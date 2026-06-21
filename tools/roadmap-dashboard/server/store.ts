import { readFile, writeFile, rename } from "node:fs/promises";
import { SCHEMA_VERSION } from "../src/types.ts";
import type { RoadmapDoc, RoadmapItem, Status, ItemKind } from "../src/types.ts";

const STATUSES: Status[] = ["shipped", "in-progress", "specced", "planned", "tentative", "blocked"];
const KINDS: ItemKind[] = ["version", "point", "feature", "task"];

function isObject(v: unknown): v is Record<string, unknown> {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}

function validateItem(value: unknown, path: string): RoadmapItem {
  if (!isObject(value)) throw new Error(`${path}: item must be an object`);
  if (typeof value.id !== "string" || value.id.length === 0) throw new Error(`${path}: item.id must be a non-empty string`);
  if (typeof value.title !== "string") throw new Error(`${path}: item.title must be a string`);
  if (!STATUSES.includes(value.status as Status)) throw new Error(`${path}: item.status invalid: ${String(value.status)}`);
  if (!KINDS.includes(value.kind as ItemKind)) throw new Error(`${path}: item.kind invalid: ${String(value.kind)}`);
  if (value.children !== undefined) {
    if (!Array.isArray(value.children)) throw new Error(`${path}: item.children must be an array`);
    value.children.forEach((c, i) => validateItem(c, `${path}.children[${i}]`));
  }
  return value as unknown as RoadmapItem;
}

/** Throws Error on any shape/schema violation; returns the doc typed on success. */
export function validateDoc(value: unknown): RoadmapDoc {
  if (!isObject(value)) throw new Error("doc must be an object");
  if (!isObject(value.meta)) throw new Error("doc.meta must be an object");
  if (value.meta.schemaVersion !== SCHEMA_VERSION) {
    throw new Error(`doc.meta.schemaVersion must be ${SCHEMA_VERSION}, got ${String(value.meta.schemaVersion)}`);
  }
  if (!Array.isArray(value.items)) throw new Error("doc.items must be an array");
  value.items.forEach((it, i) => validateItem(it, `items[${i}]`));
  if (!Array.isArray(value.continuousThreads)) throw new Error("doc.continuousThreads must be an array");
  return value as unknown as RoadmapDoc;
}

export async function loadRoadmap(path: string): Promise<RoadmapDoc> {
  const raw = await readFile(path, "utf8");
  return validateDoc(JSON.parse(raw));
}

/** Atomic write: temp file + rename, so a crash can never truncate roadmap.json. */
export async function saveRoadmap(path: string, doc: RoadmapDoc): Promise<void> {
  validateDoc(doc);
  const tmp = `${path}.tmp-${process.pid}`;
  await writeFile(tmp, JSON.stringify(doc, null, 2) + "\n", "utf8");
  await rename(tmp, path);
}
