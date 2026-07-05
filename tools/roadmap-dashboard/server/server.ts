import http from "node:http";
import { readFile } from "node:fs/promises";
import { join, normalize, extname, sep } from "node:path";
import { pathToFileURL } from "node:url";
import { loadRoadmap, saveRoadmap, validateDoc } from "./store.ts";
import { buildDecomposeRequest, writeDecomposeRequest } from "./decompose.ts";
import { listRuns, readRun, compactFinished } from "./runs.ts";
import { getCi } from "./ci.ts";
import { templates, startRun, stopRun, staleBinaryInfo } from "./control.ts";

interface Options { roadmapPath: string; rootDir: string; franklinRunsDir?: string; catalogPath?: string; }

const MIME: Record<string, string> = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
};

function send(res: http.ServerResponse, status: number, type: string, body: string): void {
  res.writeHead(status, { "content-type": type });
  res.end(body);
}

async function readBody(req: http.IncomingMessage): Promise<string> {
  const chunks: Buffer[] = [];
  for await (const c of req) chunks.push(c as Buffer);
  return Buffer.concat(chunks).toString("utf8");
}

export function createServer(opts: Options): http.Server {
  return http.createServer(async (req, res) => {
    try {
      const url = new URL(req.url ?? "/", "http://localhost");
      const path = url.pathname;

      if (path === "/api/roadmap" && req.method === "GET") {
        const doc = await loadRoadmap(opts.roadmapPath);
        return send(res, 200, MIME[".json"], JSON.stringify(doc));
      }

      if (path === "/api/roadmap" && req.method === "PUT") {
        const raw = await readBody(req);
        let doc;
        try { doc = validateDoc(JSON.parse(raw)); }
        catch (e) { return send(res, 400, MIME[".json"], JSON.stringify({ error: (e as Error).message })); }
        await saveRoadmap(opts.roadmapPath, doc);
        return send(res, 200, MIME[".json"], JSON.stringify({ ok: true }));
      }

      if (path === "/api/decompose" && req.method === "POST") {
        const raw = await readBody(req);
        const { itemId } = JSON.parse(raw) as { itemId?: string };
        const doc = await loadRoadmap(opts.roadmapPath);
        const request = itemId ? buildDecomposeRequest(doc, itemId) : null;
        if (!request) return send(res, 404, MIME[".json"], JSON.stringify({ error: "item not found" }));
        const dir = join(opts.rootDir, "requests");
        const file = await writeDecomposeRequest(dir, request);
        return send(res, 200, MIME[".json"], JSON.stringify({ prompt: request.prompt, file }));
      }

      const runsDir = opts.franklinRunsDir ?? join(opts.rootDir, "../../.franklin/runs");

      if (path === "/api/runs" && req.method === "GET") {
        const runs = await listRuns(runsDir);
        const disk = runs.reduce((sum, r) => sum + r.sizeBytes, 0);
        // Fire-and-forget: compaction is housekeeping, must not delay the response.
        compactFinished(runsDir).catch(() => { /* best-effort; next poll retries */ });
        return send(res, 200, MIME[".json"], JSON.stringify({ runs, disk }));
      }

      if (path.startsWith("/api/runs/") && req.method === "GET") {
        const id = decodeURIComponent(path.slice("/api/runs/".length));
        const detail = await readRun(runsDir, id);
        if (!detail) return send(res, 404, MIME[".json"], JSON.stringify({ error: "run not found" }));
        return send(res, 200, MIME[".json"], JSON.stringify(detail));
      }

      if (path === "/api/ci" && req.method === "GET") {
        const payload = await getCi(opts.rootDir);
        return send(res, 200, MIME[".json"], JSON.stringify(payload));
      }

      if (path === "/api/catalog" && req.method === "GET") {
        const catalogPath = opts.catalogPath ?? join(opts.rootDir, "../../docs/franklin/test-catalog.json");
        let raw: string;
        try { raw = await readFile(catalogPath, "utf8"); }
        catch { return send(res, 200, MIME[".json"], JSON.stringify({ version: 0, entries: [] })); }
        try {
          const payload = JSON.parse(raw);
          return send(res, 200, MIME[".json"], JSON.stringify(payload));
        } catch { return send(res, 500, MIME[".json"], JSON.stringify({ error: "test-catalog.json unreadable" })); }
      }

      if (path === "/api/control/templates" && req.method === "GET") {
        const stale = await staleBinaryInfo(opts.rootDir);
        return send(res, 200, MIME[".json"], JSON.stringify({ templates: templates(), stale }));
      }

      if (path === "/api/control/start" && req.method === "POST") {
        const raw = await readBody(req);
        let body;
        try { body = JSON.parse(raw); }
        catch { return send(res, 400, MIME[".json"], JSON.stringify({ ok: false, error: "invalid JSON body" })); }
        const { templateId, params } = body as { templateId?: string; params?: { model?: string; grid?: string } };
        const result = await startRun(opts.rootDir, templateId ?? "", params ?? {});
        return send(res, result.ok ? 200 : 400, MIME[".json"], JSON.stringify(result));
      }

      if (path === "/api/control/stop" && req.method === "POST") {
        const raw = await readBody(req);
        let body;
        try { body = JSON.parse(raw); }
        catch { return send(res, 400, MIME[".json"], JSON.stringify({ ok: false, error: "invalid JSON body" })); }
        const { id } = body as { id?: string };
        const result = await stopRun(opts.rootDir, runsDir, id ?? "");
        return send(res, result.ok ? 200 : 400, MIME[".json"], JSON.stringify(result));
      }

      // Static files (index.html for "/", otherwise the requested path under rootDir).
      const rel = path === "/" ? "index.html" : path.replace(/^\/+/, "");
      const safe = normalize(rel).replace(/^(\.\.[/\\])+/, "");
      const filePath = join(opts.rootDir, safe);
      const rootWithSep = opts.rootDir.endsWith(sep) ? opts.rootDir : opts.rootDir + sep;
      if (!filePath.startsWith(rootWithSep)) return send(res, 403, MIME[".html"], "Forbidden");
      try {
        const data = await readFile(filePath);
        const type = MIME[extname(filePath)] ?? "application/octet-stream";
        res.writeHead(200, { "content-type": type });
        return res.end(data);
      } catch {
        return send(res, 404, MIME[".html"], "Not found");
      }
    } catch (e) {
      return send(res, 500, MIME[".json"], JSON.stringify({ error: (e as Error).message }));
    }
  });
}

// Auto-listen when run directly (node server/server.ts).
const isMain = Boolean(process.argv[1]) && import.meta.url === pathToFileURL(process.argv[1]).href;
if (isMain) {
  const rootDir = join(import.meta.dirname, "..");
  const roadmapPath = join(rootDir, "roadmap.json");
  const port = Number(process.env.PORT ?? 4173);
  createServer({ roadmapPath, rootDir }).listen(port, "127.0.0.1", () => {
    console.log(`Bernie roadmap dashboard → http://localhost:${port}`);
  });
}
