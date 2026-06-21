import { test } from "node:test";
import assert from "node:assert/strict";
import { computeProgress } from "./progress.ts";
import type { RoadmapItem } from "./types.ts";

function leaf(status: RoadmapItem["status"]): RoadmapItem {
  return { id: "x", title: "x", status, kind: "task" };
}

test("leaf shipped = 100", () => {
  assert.equal(computeProgress(leaf("shipped")), 100);
});

test("leaf in-progress = 50", () => {
  assert.equal(computeProgress(leaf("in-progress")), 50);
});

test("leaf planned = 0", () => {
  assert.equal(computeProgress(leaf("planned")), 0);
});

test("override wins over rollup", () => {
  const item: RoadmapItem = {
    id: "v", title: "v", status: "in-progress", kind: "version",
    progressOverride: 73,
    children: [leaf("planned"), leaf("planned")],
  };
  assert.equal(computeProgress(item), 73);
});

test("rollup = shipped leaves / total leaves", () => {
  const item: RoadmapItem = {
    id: "v", title: "v", status: "in-progress", kind: "version",
    children: [leaf("shipped"), leaf("shipped"), leaf("planned"), leaf("planned")],
  };
  assert.equal(computeProgress(item), 50);
});

test("nested leaves are counted across depth", () => {
  const item: RoadmapItem = {
    id: "v", title: "v", status: "in-progress", kind: "version",
    children: [
      { id: "p", title: "p", status: "in-progress", kind: "point",
        children: [leaf("shipped"), leaf("planned")] },
      leaf("shipped"),
    ],
  };
  // leaves: shipped, planned, shipped -> 2/3 = 66.67 -> rounded 67
  assert.equal(computeProgress(item), 67);
});

test("empty children falls back to own status", () => {
  const item: RoadmapItem = {
    id: "v", title: "v", status: "shipped", kind: "version", children: [],
  };
  assert.equal(computeProgress(item), 100);
});
