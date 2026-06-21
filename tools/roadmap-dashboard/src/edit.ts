import type { RoadmapDoc, RoadmapItem, ItemKind } from "./types.ts";

export function findItem(doc: RoadmapDoc, id: string): RoadmapItem | undefined {
  const walk = (items: RoadmapItem[]): RoadmapItem | undefined => {
    for (const it of items) {
      if (it.id === id) return it;
      if (it.children) { const found = walk(it.children); if (found) return found; }
    }
    return undefined;
  };
  return walk(doc.items);
}

function mapItems(items: RoadmapItem[], fn: (it: RoadmapItem) => RoadmapItem): RoadmapItem[] {
  return items.map((it) => {
    const mapped = fn(it);
    if (mapped.children) return { ...mapped, children: mapItems(mapped.children, fn) };
    return mapped;
  });
}

export function updateItem(doc: RoadmapDoc, id: string, patch: Partial<RoadmapItem>): RoadmapDoc {
  return { ...doc, items: mapItems(doc.items, (it) => (it.id === id ? { ...it, ...patch } : it)) };
}

function removeItems(items: RoadmapItem[], id: string): RoadmapItem[] {
  return items
    .filter((it) => it.id !== id)
    .map((it) => (it.children ? { ...it, children: removeItems(it.children, id) } : it));
}

export function deleteItem(doc: RoadmapDoc, id: string): RoadmapDoc {
  return { ...doc, items: removeItems(doc.items, id) };
}

export function addChild(doc: RoadmapDoc, parentId: string, child: RoadmapItem): RoadmapDoc {
  return {
    ...doc,
    items: mapItems(doc.items, (it) =>
      it.id === parentId ? { ...it, children: [...(it.children ?? []), child] } : it),
  };
}

export function addVersion(doc: RoadmapDoc, item: RoadmapItem): RoadmapDoc {
  return { ...doc, items: [...doc.items, item] };
}

let counter = 0;
export function newItem(kind: ItemKind, title: string): RoadmapItem {
  counter += 1;
  const id = `${kind}-${Date.now().toString(36)}-${counter}`;
  return { id, title, status: "planned", kind };
}
