import type { RoadmapDoc, RoadmapItem } from "./types.ts";

function clone(items: RoadmapItem[]): RoadmapItem[] {
  return items.map((it) => ({ ...it, children: it.children ? clone(it.children) : undefined }));
}

function isDescendant(node: RoadmapItem, id: string): boolean {
  if (node.id === id) return true;
  return (node.children ?? []).some((c) => isDescendant(c, id));
}

/** Remove the item with `id` from the tree, returning it (or null). Mutates `items`. */
function extract(items: RoadmapItem[], id: string): RoadmapItem | null {
  const idx = items.findIndex((it) => it.id === id);
  if (idx >= 0) { const [removed] = items.splice(idx, 1); return removed; }
  for (const it of items) {
    if (it.children) { const r = extract(it.children, id); if (r) return r; }
  }
  return null;
}

function insertRelative(items: RoadmapItem[], targetId: string, node: RoadmapItem, before: boolean): boolean {
  const idx = items.findIndex((it) => it.id === targetId);
  if (idx >= 0) { items.splice(before ? idx : idx + 1, 0, node); return true; }
  for (const it of items) {
    if (it.children && insertRelative(it.children, targetId, node, before)) return true;
  }
  return false;
}

function insertInside(items: RoadmapItem[], targetId: string, node: RoadmapItem): boolean {
  for (const it of items) {
    if (it.id === targetId) { it.children = [...(it.children ?? []), node]; return true; }
    if (it.children && insertInside(it.children, targetId, node)) return true;
  }
  return false;
}

export function moveItem(
  doc: RoadmapDoc,
  draggedId: string,
  targetId: string,
  position: "before" | "after" | "inside",
): RoadmapDoc {
  if (draggedId === targetId) return doc;
  const dragged = findRaw(doc.items, draggedId);
  if (!dragged || !findRaw(doc.items, targetId)) return doc;
  if (isDescendant(dragged, targetId)) return doc; // can't drop into own subtree

  const items = clone(doc.items);
  const node = extract(items, draggedId);
  if (!node) return doc;

  const ok = position === "inside"
    ? insertInside(items, targetId, node)
    : insertRelative(items, targetId, node, position === "before");
  if (!ok) return doc; // target vanished (shouldn't happen) — bail without losing data
  return { ...doc, items };
}

function findRaw(items: RoadmapItem[], id: string): RoadmapItem | undefined {
  for (const it of items) {
    if (it.id === id) return it;
    if (it.children) { const f = findRaw(it.children, id); if (f) return f; }
  }
  return undefined;
}
