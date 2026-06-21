import { test } from "node:test";
import assert from "node:assert/strict";
import { renderDoc, renderItem, escapeHtml } from "./render.ts";
import type { RoadmapDoc, RoadmapItem } from "./types.ts";

test("escapeHtml neutralizes angle brackets and ampersands", () => {
  assert.equal(escapeHtml(`a<b>&"c`), `a&lt;b&gt;&amp;&quot;c`);
});

test("renderItem includes title, status badge, and a data-id", () => {
  const item: RoadmapItem = { id: "v5", title: "v5 — Spine", status: "in-progress", kind: "version" };
  const html = renderItem(item);
  assert.match(html, /data-id="v5"/);
  assert.match(html, /v5 — Spine/);
  assert.match(html, /in-progress/);
});

test("renderItem shows a progress bar with the rolled-up percent", () => {
  const item: RoadmapItem = {
    id: "v5", title: "v5", status: "in-progress", kind: "version",
    children: [
      { id: "a", title: "a", status: "shipped", kind: "point" },
      { id: "b", title: "b", status: "planned", kind: "point" },
    ],
  };
  const html = renderItem(item);
  assert.match(html, /width:\s*50%/);
});

test("renderItem recurses into children", () => {
  const item: RoadmapItem = {
    id: "v5", title: "v5", status: "in-progress", kind: "version",
    children: [{ id: "v5.1", title: "hot-swap", status: "planned", kind: "point" }],
  };
  const html = renderItem(item);
  assert.match(html, /data-id="v5.1"/);
  assert.match(html, /hot-swap/);
});

test("renderDoc renders meta header and every top-level item", () => {
  const doc: RoadmapDoc = {
    meta: { product: "Bernie", tagline: "tag", currentVersion: "5", nextStep: "next!", lastUpdated: "2026-06-20", schemaVersion: 1 },
    items: [
      { id: "v5", title: "v5", status: "in-progress", kind: "version" },
      { id: "v6", title: "v6", status: "planned", kind: "version" },
    ],
    continuousThreads: [{ id: "t1", title: "harness" }],
  };
  const html = renderDoc(doc);
  assert.match(html, /Bernie/);
  assert.match(html, /next!/);
  assert.match(html, /data-id="v5"/);
  assert.match(html, /data-id="v6"/);
  assert.match(html, /harness/);
});
