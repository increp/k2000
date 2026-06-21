import type { RoadmapItem } from "./types.ts";

function statusToPercent(status: RoadmapItem["status"]): number {
  if (status === "shipped") return 100;
  if (status === "in-progress") return 50;
  return 0;
}

function collectLeaves(item: RoadmapItem, out: RoadmapItem[]): void {
  if (!item.children || item.children.length === 0) {
    out.push(item);
    return;
  }
  for (const child of item.children) collectLeaves(child, out);
}

/** 0..100. Manual override wins; otherwise roll up from leaf descendants. */
export function computeProgress(item: RoadmapItem): number {
  if (typeof item.progressOverride === "number") {
    return Math.max(0, Math.min(100, item.progressOverride));
  }
  if (!item.children || item.children.length === 0) {
    return statusToPercent(item.status);
  }
  const leaves: RoadmapItem[] = [];
  collectLeaves(item, leaves);
  if (leaves.length === 0) return statusToPercent(item.status);
  const shipped = leaves.filter((l) => l.status === "shipped").length;
  return Math.round((shipped / leaves.length) * 100);
}
