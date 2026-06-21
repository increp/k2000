import http from "node:http";
import { readFile } from "node:fs/promises";
import { join, normalize, extname, sep } from "node:path";
import { pathToFileURL } from "node:url";
import { loadRoadmap, saveRoadmap, validateDoc } from "./store.ts";

interface Options { roadmapPath: string; rootDir: string; }

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
  createServer({ roadmapPath, rootDir }).listen(port, () => {
    console.log(`Bernie roadmap dashboard → http://localhost:${port}`);
  });
}
