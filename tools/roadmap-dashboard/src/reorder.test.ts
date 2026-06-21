import { test } from "node:test";
import assert from "node:assert/strict";
import { moveItem } from "./reorder.ts";
import { findItem } from "./edit.ts";
import type { RoadmapDoc } from "./types.ts";

function doc(): RoadmapDoc {
  return {
    meta: { product: "Bernie", tagline: "t", currentVersion: "5", nextStep: "n", lastUpdated: "2026-06-20", schemaVersion: 1 },
    items: [
      { id: "v5", title: "v5", status: "in-progress", kind: "version",
        children: [
          { id: "v5.1", title: "a", status: "planned", kind: "point" },
          { id: "v5.2", title: "b", status: "planned", kind: "point" },
        ] },
      { id: "v6", title: "v6", status: "planned", kind: "version" },
      { id: "v7", title: "v7", status: "planned", kind: "version" },
    ],
    continuousThreads: [],
  };
}

test("move a top-level item before another reorders the list", () => {
  const after = moveItem(doc(), "v7", "v6", "before");
  assert.deepEqual(after.items.map((i) => i.id), ["v5", "v7", "v6"]);
});

test("move a top-level item after another", () => {
  const after = moveItem(doc(), "v5", "v6", "after");
  assert.deepEqual(after.items.map((i) => i.id), ["v6", "v5", "v7"]);
});

test("reorder children within a parent", () => {
  const after = moveItem(doc(), "v5.2", "v5.1", "before");
  assert.deepEqual(findItem(after, "v5")?.children?.map((c) => c.id), ["v5.2", "v5.1"]);
});

test("re-parent a child onto a top-level item with 'inside'", () => {
  const after = moveItem(doc(), "v5.1", "v6", "inside");
  assert.equal(findItem(after, "v5")?.children?.length, 1);
  assert.deepEqual(findItem(after, "v6")?.children?.map((c) => c.id), ["v5.1"]);
});

test("dropping an item onto itself is a no-op", () => {
  const before = doc();
  const after = moveItem(before, "v6", "v6", "before");
  assert.deepEqual(after.items.map((i) => i.id), before.items.map((i) => i.id));
});

test("dropping a parent into its own descendant is a no-op", () => {
  const before = doc();
  const after = moveItem(before, "v5", "v5.1", "inside");
  assert.deepEqual(after, before);
});

test("unknown ids are a no-op", () => {
  const before = doc();
  assert.deepEqual(moveItem(before, "nope", "v6", "before"), before);
});
