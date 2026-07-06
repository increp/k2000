import { getRoadmap, putRoadmap } from "./api.ts";
import { renderDoc } from "./render.ts";
import { findItem, updateItem, deleteItem, addChild, addVersion, newItem } from "./edit.ts";
import { moveItem } from "./reorder.ts";
import { mountFranklin, unmountFranklin } from "./franklin.ts";
import type { RoadmapDoc, RoadmapItem, Status, ItemKind } from "./types.ts";

let doc: RoadmapDoc;
const app = () => document.getElementById("app") as HTMLElement;

// Which top-level view owns #app. Roadmap is the default and its behavior stays
// byte-identical whenever the Franklin view is never opened.
type View = "roadmap" | "franklin";
let view: View = "roadmap";

function paint(): void {
  // Roadmap only paints while it owns #app; Franklin manages its own DOM.
  if (view === "roadmap") app().innerHTML = renderDoc(doc);
}

async function commit(next: RoadmapDoc): Promise<void> {
  doc = next;
  paint();
  await putRoadmap(doc);
}

const STATUS_CYCLE: Status[] = ["planned", "specced", "in-progress", "shipped", "blocked", "tentative"];

function itemEl(target: HTMLElement): HTMLElement | null {
  return target.closest<HTMLElement>(".item");
}

async function onClick(ev: MouseEvent): Promise<void> {
  const t = ev.target as HTMLElement;
  const action = t.dataset.action;
  if (!action) return;
  const el = itemEl(t);
  const id = el?.dataset.id;

  if (action === "add-version") {
    const title = prompt("New version title?", "v26 — ");
    if (title) await commit(addVersion(doc, newItem("version", title)));
    return;
  }
  if (!id) return;
  const item = findItem(doc, id);
  if (!item) return;

  if (action === "edit") {
    const title = prompt("Title:", item.title);
    if (title === null) return; // Escape / cancel — no-op
    if (title !== item.title) { await commit(updateItem(doc, id, { title })); return; }
    // If the title is unchanged, offer a status cycle instead.
    const cur = STATUS_CYCLE.indexOf(item.status);
    const next = STATUS_CYCLE[(cur + 1) % STATUS_CYCLE.length];
    await commit(updateItem(doc, id, { status: next }));
  } else if (action === "delete") {
    if (confirm(`Delete "${item.title}" and its sub-items?`)) await commit(deleteItem(doc, id));
  } else if (action === "add-child") {
    const kind: ItemKind = item.kind === "version" ? "point" : item.kind === "point" ? "feature" : "task";
    const title = prompt(`New ${kind} under "${item.title}":`, "");
    if (title) await commit(addChild(doc, id, newItem(kind, title)));
  } else if (action === "decompose") {
    await onDecompose(id); // implemented in Task 7
  }
}

async function refresh(): Promise<void> {
  doc = await getRoadmap();
  paint();
}

app().addEventListener("click", (ev) => { void onClick(ev as MouseEvent); });

// --- View switching (Roadmap / Franklin tabs) -------------------------------

function setActiveTab(next: View): void {
  const nav = document.getElementById("tabs");
  if (!nav) return;
  for (const btn of nav.querySelectorAll<HTMLButtonElement>("button[data-view]")) {
    btn.classList.toggle("active", btn.dataset.view === next);
  }
}

function switchView(next: View): void {
  if (next === view) return;
  if (view === "franklin") unmountFranklin(); // tear down Franklin's timers/listeners
  view = next;
  setActiveTab(next);
  app().classList.toggle("franklin-view", next === "franklin");
  if (next === "roadmap") {
    paint(); // repaint roadmap from the in-memory doc (byte-identical to first render)
  } else {
    mountFranklin(app());
  }
}

const tabsNav = document.getElementById("tabs");
if (tabsNav) {
  tabsNav.addEventListener("click", (ev) => {
    const btn = (ev.target as HTMLElement).closest<HTMLButtonElement>("button[data-view]");
    const next = btn?.dataset.view;
    if (next === "roadmap" || next === "franklin") switchView(next);
  });
  setActiveTab(view);
}

refresh().catch((e) => { app().textContent = `Failed to load roadmap: ${(e as Error).message}`; });

async function onDecompose(id: string): Promise<void> {
  const r = await fetch("/api/decompose", {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ itemId: id }),
  });
  if (!r.ok) { alert(`Decompose failed: ${r.status}`); return; }
  const { prompt, file } = await r.json() as { prompt: string; file: string };
  try { await navigator.clipboard.writeText(prompt); } catch { /* clipboard may be blocked; prompt still shown */ }
  alert(
    `Request written to:\n${file}\n\n` +
    `The decomposition prompt has been copied to your clipboard — paste it into Claude Code.\n\n` +
    `Prompt:\n${prompt}\n\n` +
    `When Claude finishes, RELOAD this page before making any further edits — ` +
    `otherwise an edit from this stale tab could overwrite the tasks Claude just added.`,
  );
}

let draggedId: string | null = null;

app().addEventListener("dragstart", (ev) => {
  const el = (ev.target as HTMLElement).closest<HTMLElement>(".item");
  if (el) { draggedId = el.dataset.id ?? null; ev.dataTransfer?.setData("text/plain", draggedId ?? ""); }
});

app().addEventListener("dragover", (ev) => {
  const el = (ev.target as HTMLElement).closest<HTMLElement>(".item");
  if (el && draggedId && el.dataset.id !== draggedId) { ev.preventDefault(); el.classList.add("drag-over"); }
});

app().addEventListener("dragleave", (ev) => {
  (ev.target as HTMLElement).closest<HTMLElement>(".item")?.classList.remove("drag-over");
});

app().addEventListener("drop", (ev) => {
  ev.preventDefault();
  const el = (ev.target as HTMLElement).closest<HTMLElement>(".item");
  el?.classList.remove("drag-over");
  const targetId = el?.dataset.id;
  if (!draggedId || !targetId || draggedId === targetId) return;
  // Drop in the upper third = before, lower third = after, middle = inside.
  const rect = el!.getBoundingClientRect();
  const offset = (ev as DragEvent).clientY - rect.top;
  const third = rect.height / 3;
  const position: "before" | "after" | "inside" = offset < third ? "before" : offset > 2 * third ? "after" : "inside";
  void commit(moveItem(doc, draggedId, targetId, position));
  draggedId = null;
});

export { commit, doc };
