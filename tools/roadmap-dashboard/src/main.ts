import { getRoadmap } from "./api.ts";
import { renderDoc } from "./render.ts";
import type { RoadmapDoc } from "./types.ts";

let doc: RoadmapDoc;

async function refresh(): Promise<void> {
  doc = await getRoadmap();
  const app = document.getElementById("app");
  if (app) app.innerHTML = renderDoc(doc);
}

refresh().catch((e) => {
  const app = document.getElementById("app");
  if (app) app.textContent = `Failed to load roadmap: ${(e as Error).message}`;
});
