import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, writeFile, mkdir } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import type { AddressInfo } from "node:net";
import { createServer } from "./server.ts";
import type { RoadmapDoc } from "../src/types.ts";

const here = dirname(fileURLToPath(import.meta.url));
const rootDir = join(here, "..");

function doc(): RoadmapDoc {
  return {
    meta: { product: "Bernie", tagline: "t", currentVersion: "5", nextStep: "n", lastUpdated: "2026-06-20", schemaVersion: 1 },
    items: [{ id: "v5", title: "v5", status: "in-progress", kind: "version" }],
    continuousThreads: [],
  };
}

const SUITE_START = '{"ev":"start","ts":1783116948810,"kind":"suite","argv":["./build/tests/k2000_tests"],"pid":342689,"buildType":"Release","gitSha":"de6bff5"}';
const SUITE_END_PASS = '{"ev":"end","ts":1783116965554,"outcome":"pass","durationS":16.744,"tests":291,"failed":0,"checks":[]}';

let base = "";
let server: ReturnType<typeof createServer>;
let roadmapPath = "";
let franklinRunsDir = "";

before(async () => {
  const dir = await mkdtemp(join(tmpdir(), "rm-srv-"));
  roadmapPath = join(dir, "roadmap.json");
  await writeFile(roadmapPath, JSON.stringify(doc(), null, 2));

  franklinRunsDir = join(dir, "franklin-runs");
  await mkdir(franklinRunsDir, { recursive: true });
  await writeFile(join(franklinRunsDir, "20260703-161548-suite-342689.ndjson"), SUITE_START + "\n" + SUITE_END_PASS + "\n");

  server = createServer({ roadmapPath, rootDir, franklinRunsDir });
  await new Promise<void>((res) => server.listen(0, res));
  const port = (server.address() as AddressInfo).port;
  base = `http://127.0.0.1:${port}`;
});

after(() => server.close());

test("GET /api/roadmap returns the document", async () => {
  const r = await fetch(`${base}/api/roadmap`);
  assert.equal(r.status, 200);
  const body = await r.json() as RoadmapDoc;
  assert.equal(body.items[0].id, "v5");
});

test("PUT /api/roadmap persists a valid document", async () => {
  const next = doc();
  next.meta.nextStep = "changed";
  const put = await fetch(`${base}/api/roadmap`, {
    method: "PUT", headers: { "content-type": "application/json" }, body: JSON.stringify(next),
  });
  assert.equal(put.status, 200);
  const get = await fetch(`${base}/api/roadmap`);
  const body = await get.json() as RoadmapDoc;
  assert.equal(body.meta.nextStep, "changed");
});

test("PUT of an invalid document is rejected and leaves the file unchanged", async () => {
  const bad = { meta: { schemaVersion: 1 }, items: "nope", continuousThreads: [] };
  const put = await fetch(`${base}/api/roadmap`, {
    method: "PUT", headers: { "content-type": "application/json" }, body: JSON.stringify(bad),
  });
  assert.equal(put.status, 400);
  const get = await fetch(`${base}/api/roadmap`);
  const body = await get.json() as RoadmapDoc;
  assert.equal(body.items[0].id, "v5"); // unchanged from prior test's valid PUT
});

test("GET / serves the HTML shell", async () => {
  const r = await fetch(`${base}/`);
  assert.equal(r.status, 200);
  assert.match(r.headers.get("content-type") ?? "", /text\/html/);
  const text = await r.text();
  assert.match(text, /id="app"/);
});

test("POST /api/decompose returns a prompt and writes a request file", async () => {
  // The seed test doc only has v5; add a child-bearing item via PUT first.
  const cur = await (await fetch(`${base}/api/roadmap`)).json() as RoadmapDoc;
  cur.items.push({ id: "v8", title: "v8 — Ricky", status: "planned", kind: "version" });
  await fetch(`${base}/api/roadmap`, { method: "PUT", headers: { "content-type": "application/json" }, body: JSON.stringify(cur) });

  const r = await fetch(`${base}/api/decompose`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ itemId: "v8" }),
  });
  assert.equal(r.status, 200);
  const body = await r.json() as { prompt: string; file: string };
  assert.match(body.prompt, /roadmap\.json/);
  assert.match(body.file, /v8\.json$/);
});

test("POST /api/decompose 404s for an unknown item", async () => {
  const r = await fetch(`${base}/api/decompose`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ itemId: "nope" }),
  });
  assert.equal(r.status, 404);
});

// --- /api/runs ---------------------------------------------------------------

test("GET /api/runs returns the seeded tmp run and a disk byte sum", async () => {
  const r = await fetch(`${base}/api/runs`);
  assert.equal(r.status, 200);
  const body = await r.json() as { runs: Array<{ id: string; sizeBytes: number }>; disk: number };
  assert.equal(body.runs.length, 1);
  assert.equal(body.runs[0].id, "20260703-161548-suite-342689.ndjson");
  assert.equal(typeof body.disk, "number");
  assert.equal(body.disk, body.runs[0].sizeBytes);
});

test("GET /api/runs/<id> returns the run detail", async () => {
  const r = await fetch(`${base}/api/runs/20260703-161548-suite-342689.ndjson`);
  assert.equal(r.status, 200);
  const body = await r.json() as { id: string; testsList: unknown[] };
  assert.equal(body.id, "20260703-161548-suite-342689.ndjson");
});

test("GET /api/runs/<unknown-id> 404s", async () => {
  const r = await fetch(`${base}/api/runs/does-not-exist.ndjson`);
  assert.equal(r.status, 404);
});

test("GET /api/runs/<traversal id> decodes to a null-detail path and 404s", async () => {
  // "..%2Fetc%2Fpasswd" decodes to "../etc/passwd" — readRun must reject it, not the router.
  const r = await fetch(`${base}/api/runs/..%2Fetc%2Fpasswd`);
  assert.equal(r.status, 404);
});

// --- /api/ci -------------------------------------------------------------------

// The parser fixtures + empty-PATH unavailable test in ci.test.ts already cover the code paths.

// --- /api/control/templates ----------------------------------------------------

test("GET /api/control/templates returns {templates, stale}", async () => {
  const r = await fetch(`${base}/api/control/templates`);
  assert.equal(r.status, 200);
  const body = await r.json() as { templates: Array<{ id: string }>; stale: Array<{ binary: string }> };
  assert.ok(Array.isArray(body.templates));
  assert.ok(body.templates.some((t) => t.id === "suite"));
  assert.ok(Array.isArray(body.stale));
});

// --- /api/control/start & /api/control/stop -------------------------------------

test("POST /api/control/start with an unknown template returns 400", async () => {
  const r = await fetch(`${base}/api/control/start`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ templateId: "not-a-template", params: {} }),
  });
  assert.equal(r.status, 400);
  const body = await r.json() as { ok: boolean; error: string };
  assert.equal(body.ok, false);
  assert.match(body.error, /template/i);
});

test("POST /api/control/stop for a run with no start event returns 400", async () => {
  const r = await fetch(`${base}/api/control/stop`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ id: "does-not-exist.ndjson" }),
  });
  assert.equal(r.status, 400);
  const body = await r.json() as { ok: boolean; error: string };
  assert.equal(body.ok, false);
});

test("POST /api/control/start with malformed JSON body returns 400 and error", async () => {
  const r = await fetch(`${base}/api/control/start`, {
    method: "POST", headers: { "content-type": "application/json" }, body: "{not json",
  });
  assert.equal(r.status, 400);
  const body = await r.json() as { ok: boolean; error: string };
  assert.equal(body.ok, false);
  assert.match(body.error, /invalid JSON/i);
});
