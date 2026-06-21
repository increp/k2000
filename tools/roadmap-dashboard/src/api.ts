import type { RoadmapDoc } from "./types.ts";

export async function getRoadmap(): Promise<RoadmapDoc> {
  const r = await fetch("/api/roadmap");
  if (!r.ok) throw new Error(`GET /api/roadmap failed: ${r.status}`);
  return r.json() as Promise<RoadmapDoc>;
}

export async function putRoadmap(doc: RoadmapDoc): Promise<void> {
  const r = await fetch("/api/roadmap", {
    method: "PUT",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(doc),
  });
  if (!r.ok) {
    const msg = await r.text();
    throw new Error(`PUT /api/roadmap failed: ${r.status} ${msg}`);
  }
}
