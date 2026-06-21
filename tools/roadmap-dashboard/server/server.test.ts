import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, writeFile } from "node:fs/promises";
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

let base = "";
let server: ReturnType<typeof createServer>;
let roadmapPath = "";

before(async () => {
  const dir = await mkdtemp(join(tmpdir(), "rm-srv-"));
  roadmapPath = join(dir, "roadmap.json");
  await writeFile(roadmapPath, JSON.stringify(doc(), null, 2));
  server = createServer({ roadmapPath, rootDir });
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
