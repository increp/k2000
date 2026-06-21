import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, readFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { loadRoadmap, saveRoadmap, validateDoc } from "./store.ts";
import type { RoadmapDoc } from "../src/types.ts";

function minimalDoc(): RoadmapDoc {
  return {
    meta: { product: "Bernie", tagline: "t", currentVersion: "5", nextStep: "n", lastUpdated: "2026-06-20", schemaVersion: 1 },
    items: [{ id: "v5", title: "v5", status: "in-progress", kind: "version" }],
    continuousThreads: [],
  };
}

test("save then load round-trips", async () => {
  const dir = await mkdtemp(join(tmpdir(), "rm-"));
  const path = join(dir, "roadmap.json");
  const doc = minimalDoc();
  await saveRoadmap(path, doc);
  const loaded = await loadRoadmap(path);
  assert.deepEqual(loaded, doc);
});

test("save writes pretty JSON ending with newline", async () => {
  const dir = await mkdtemp(join(tmpdir(), "rm-"));
  const path = join(dir, "roadmap.json");
  await saveRoadmap(path, minimalDoc());
  const raw = await readFile(path, "utf8");
  assert.ok(raw.endsWith("\n"));
  assert.ok(raw.includes("\n  "));
});

test("validateDoc rejects wrong schema version", () => {
  const bad = { ...minimalDoc(), meta: { ...minimalDoc().meta, schemaVersion: 999 } };
  assert.throws(() => validateDoc(bad), /schemaVersion/);
});

test("validateDoc rejects missing items array", () => {
  assert.throws(() => validateDoc({ meta: minimalDoc().meta, continuousThreads: [] }), /items/);
});

test("validateDoc rejects an item with an unknown status", () => {
  const bad = minimalDoc();
  (bad.items[0] as { status: string }).status = "frobnicated";
  assert.throws(() => validateDoc(bad), /status/);
});

test("validateDoc accepts a valid doc and returns it", () => {
  const doc = minimalDoc();
  assert.deepEqual(validateDoc(doc), doc);
});
