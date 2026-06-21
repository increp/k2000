import { writeFile, mkdir } from "node:fs/promises";
import { join } from "node:path";
import type { RoadmapDoc, RoadmapItem } from "../src/types.ts";

export interface DecomposeRequest {
  itemId: string;
  title: string;
  ancestry: string[];
  currentChildren: RoadmapItem[];
  prompt: string;
}

function locate(items: RoadmapItem[], id: string, trail: string[]): { item: RoadmapItem; ancestry: string[] } | null {
  for (const it of items) {
    const here = [...trail, it.id];
    if (it.id === id) return { item: it, ancestry: here };
    if (it.children) { const f = locate(it.children, id, here); if (f) return f; }
  }
  return null;
}

export function buildDecomposeRequest(doc: RoadmapDoc, itemId: string): DecomposeRequest | null {
  const found = locate(doc.items, itemId, []);
  if (!found) return null;
  const { item, ancestry } = found;
  const prompt =
    `Decompose roadmap item ${item.id} ("${item.title}") into 4–8 concrete, independently ` +
    `testable tasks. Read tools/roadmap-dashboard/roadmap.json, append each task as a child ` +
    `(kind "task", status "planned") of the item with id "${item.id}", preserving its existing ` +
    `children, then save the file (atomic write). Keep titles short; put detail in each task's ` +
    `"summary". Do not change any other item.`;
  return { itemId: item.id, title: item.title, ancestry, currentChildren: item.children ?? [], prompt };
}

function safeName(id: string): string {
  return id.replace(/[^a-zA-Z0-9._-]/g, "_");
}

export async function writeDecomposeRequest(dir: string, req: DecomposeRequest): Promise<string> {
  await mkdir(dir, { recursive: true });
  const file = join(dir, `${safeName(req.itemId)}.json`);
  await writeFile(file, JSON.stringify(req, null, 2) + "\n", "utf8");
  return file;
}
