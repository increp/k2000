import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, readFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { buildDecomposeRequest, writeDecomposeRequest } from "./decompose.ts";
import type { RoadmapDoc } from "../src/types.ts";

function doc(): RoadmapDoc {
  return {
    meta: { product: "Bernie", tagline: "t", currentVersion: "5", nextStep: "n", lastUpdated: "2026-06-20", schemaVersion: 1 },
    items: [
      { id: "v8", title: "v8 — Ricky: The FX Section", status: "planned", kind: "version",
        children: [{ id: "v8-aux", title: "aux chains", status: "planned", kind: "point" }] },
    ],
    continuousThreads: [],
  };
}

test("buildDecomposeRequest returns the item, its children, and a prompt referencing roadmap.json", () => {
  const req = buildDecomposeRequest(doc(), "v8");
  assert.ok(req);
  assert.equal(req!.itemId, "v8");
  assert.match(req!.title, /Ricky/);
  assert.equal(req!.currentChildren.length, 1);
  assert.match(req!.prompt, /roadmap\.json/);
  assert.match(req!.prompt, /"v8"/);
});

test("buildDecomposeRequest returns null for an unknown id", () => {
  assert.equal(buildDecomposeRequest(doc(), "nope"), null);
});

test("writeDecomposeRequest writes <itemId>.json into the dir", async () => {
  const dir = await mkdtemp(join(tmpdir(), "rm-dec-"));
  const req = buildDecomposeRequest(doc(), "v8")!;
  const file = await writeDecomposeRequest(dir, req);
  assert.match(file, /v8\.json$/);
  const parsed = JSON.parse(await readFile(file, "utf8"));
  assert.equal(parsed.itemId, "v8");
});

test("writeDecomposeRequest sanitizes ids so they cannot escape the dir", async () => {
  const dir = await mkdtemp(join(tmpdir(), "rm-dec-"));
  const req = { itemId: "../../etc/passwd", title: "x", ancestry: [], currentChildren: [], prompt: "p" };
  const file = await writeDecomposeRequest(dir, req);
  assert.ok(file.startsWith(dir), `expected ${file} to stay under ${dir}`);
});
