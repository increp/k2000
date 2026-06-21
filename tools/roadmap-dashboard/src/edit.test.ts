import { test } from "node:test";
import assert from "node:assert/strict";
import { findItem, updateItem, deleteItem, addChild, addVersion, newItem } from "./edit.ts";
import type { RoadmapDoc } from "./types.ts";

function doc(): RoadmapDoc {
  return {
    meta: { product: "Bernie", tagline: "t", currentVersion: "5", nextStep: "n", lastUpdated: "2026-06-20", schemaVersion: 1 },
    items: [
      { id: "v5", title: "v5", status: "in-progress", kind: "version",
        children: [{ id: "v5.1", title: "hot-swap", status: "planned", kind: "point" }] },
      { id: "v6", title: "v6", status: "planned", kind: "version" },
    ],
    continuousThreads: [],
  };
}

test("findItem locates nested items", () => {
  assert.equal(findItem(doc(), "v5.1")?.title, "hot-swap");
  assert.equal(findItem(doc(), "nope"), undefined);
});

test("updateItem patches a nested item without mutating the input", () => {
  const before = doc();
  const after = updateItem(before, "v5.1", { status: "in-progress", title: "Hot-swap!" });
  assert.equal(findItem(after, "v5.1")?.status, "in-progress");
  assert.equal(findItem(after, "v5.1")?.title, "Hot-swap!");
  assert.equal(findItem(before, "v5.1")?.status, "planned"); // input untouched
});

test("deleteItem removes a nested item", () => {
  const after = deleteItem(doc(), "v5.1");
  assert.equal(findItem(after, "v5.1"), undefined);
  assert.equal(findItem(after, "v5")?.children?.length ?? 0, 0);
});

test("deleteItem removes a top-level item", () => {
  const after = deleteItem(doc(), "v6");
  assert.equal(after.items.length, 1);
});

test("addChild appends under the named parent", () => {
  const child = newItem("task", "do the thing");
  const after = addChild(doc(), "v5", child);
  const kids = findItem(after, "v5")?.children ?? [];
  assert.equal(kids.length, 2);
  assert.equal(kids[1].title, "do the thing");
});

test("addVersion appends at the top level", () => {
  const after = addVersion(doc(), newItem("version", "v26"));
  assert.equal(after.items.length, 3);
  assert.equal(after.items[2].title, "v26");
});

test("newItem generates unique ids and sane defaults", () => {
  const a = newItem("task", "x");
  const b = newItem("task", "x");
  assert.notEqual(a.id, b.id);
  assert.equal(a.kind, "task");
  assert.equal(a.status, "planned");
});
