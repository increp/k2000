import type { RoadmapDoc, RoadmapItem } from "./types.ts";
import { computeProgress } from "./progress.ts";

export function escapeHtml(s: string): string {
  return s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function tagBadges(item: RoadmapItem): string {
  if (!item.tags || item.tags.length === 0) return "";
  return item.tags.map((t) => `<span class="tag tag-${t}">${escapeHtml(t)}</span>`).join("");
}

function specLinks(item: RoadmapItem): string {
  if (!item.specLinks || item.specLinks.length === 0) return "";
  return `<div class="spec-links">` +
    item.specLinks.map((l) => `<a href="#" data-spec="${escapeHtml(l)}">${escapeHtml(l.split("/").pop() ?? l)}</a>`).join(" ") +
    `</div>`;
}

export function renderItem(item: RoadmapItem): string {
  const pct = computeProgress(item);
  const summary = item.summary ? `<div class="summary">${escapeHtml(item.summary)}</div>` : "";
  const children = item.children && item.children.length > 0
    ? `<div class="children">${item.children.map(renderItem).join("")}</div>`
    : "";
  return `
    <div class="item kind-${item.kind} status-${item.status}" draggable="true" data-id="${escapeHtml(item.id)}">
      <div class="item-head">
        <span class="drag-handle" title="Drag to reorder">⋮⋮</span>
        <span class="title" data-action="edit">${escapeHtml(item.title)}</span>
        <span class="status-badge">${escapeHtml(item.status)}</span>
        ${tagBadges(item)}
        <span class="spacer"></span>
        <button data-action="add-child" title="Add sub-item">＋</button>
        <button data-action="decompose" title="Decompose with Claude">⚡</button>
        <button data-action="delete" title="Delete">✕</button>
      </div>
      <div class="progress" title="${pct}%"><div class="bar" style="width: ${pct}%"></div></div>
      ${summary}
      ${specLinks(item)}
      ${children}
    </div>`;
}

export function renderDoc(doc: RoadmapDoc): string {
  const m = doc.meta;
  const threads = doc.continuousThreads
    .map((t) => `<li title="${escapeHtml(t.summary ?? "")}">${escapeHtml(t.title)}</li>`).join("");
  const items = doc.items.map(renderItem).join("");
  return `
    <header class="topbar">
      <div class="brand"><h1>${escapeHtml(m.product)}</h1><p>${escapeHtml(m.tagline)}</p></div>
      <div class="now">
        <span class="pill">You are here: v${escapeHtml(m.currentVersion)}</span>
        <span class="next">Next: ${escapeHtml(m.nextStep)}</span>
        <span class="updated">Updated ${escapeHtml(m.lastUpdated)}</span>
      </div>
    </header>
    <section class="filters" data-role="filters"></section>
    <main class="timeline">${items}
      <button class="add-version" data-action="add-version">＋ Add version</button>
    </main>
    <aside class="threads"><h2>Continuous threads</h2><ul>${threads}</ul></aside>`;
}
