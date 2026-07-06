// Franklin dashboard — the live view controller. Fetches run/CI/catalog/template
// state, polls on timers, delegates all UI events by data-action (main.ts idiom),
// and hands the string HTML from franklinRender.ts to the DOM.
//
// Stable-DOM rendering (the dropdown fix): mount builds FIVE persistent
// <section data-fr="..."> containers ONCE; each poller rewrites ONLY its own
// section's innerHTML. A native <select> the user has opened lives inside the
// form section, which — thanks to the guards below — is never touched while it
// holds focus and never touched in steady state, so the browser never closes it.
//   Guard (a): the form section re-renders only when JSON.stringify({templates,
//     stale}) differs from the last render — steady-state = zero DOM writes.
//   Guard (b): paintSection refuses to repaint any section that contains
//     document.activeElement; it stashes the pending HTML and a focusout flush
//     applies it on the next tick once focus has left.
//
// Type-only imports for server modules (erased at bundle time — no node:* leaks).
import { renderActive, renderCiStrip, renderForm, renderArchive, renderRunDetail } from "./franklinRender.ts";
import type { RunSummary, RunDetail } from "./franklinTypes.ts";
import type { CiPayload } from "../server/ci.ts";
import type { StaleInfo, Template } from "../server/control.ts";
import type { CatalogEntry } from "./franklinExplain.ts";

const RUNS_POLL_MS = 2_000;
const CI_POLL_MS = 10_000;

interface FranklinState {
  runs: RunSummary[];
  disk: number;
  ci: CiPayload;
  stale: StaleInfo[];
  templates: Template[];
  catalog: CatalogEntry[];
  catalogError: boolean;
  openDetailId: string | null;
  /** Last-fetched detail for openDetailId, reused by repaint when the run is finished. */
  openDetailData: RunDetail | null;
}

const EMPTY_CI: CiPayload = { available: false, fetchedAt: 0, branches: [] };

function emptyState(): FranklinState {
  return {
    runs: [], disk: 0, ci: EMPTY_CI, stale: [], templates: [], catalog: [], catalogError: false,
    openDetailId: null, openDetailData: null,
  };
}

let host: HTMLElement | null = null;
let state: FranklinState = emptyState();
const timers: ReturnType<typeof setInterval>[] = [];
let clickHandler: ((ev: Event) => void) | null = null;
let submitHandler: ((ev: Event) => void) | null = null;
let changeHandler: ((ev: Event) => void) | null = null;
let focusoutHandler: ((ev: Event) => void) | null = null;

// Per-section HTML that paintSection deferred because the section held focus.
// Flushed on the next tick after a focusout. Keyed by data-fr section name.
const pending = new Map<string, string>();
// Last {templates, stale} JSON the form section rendered — guard (a). null until
// the first form paint, so the first data arrival always renders once.
let lastFormPayload: string | null = null;

// Bumped on every mount/unmount. Async refreshers capture the generation
// before their first await and bail before touching state/DOM if it has
// since changed — otherwise a late resolver from a stale mount (e.g. a
// fetch that outlives an unmount+remount cycle in tests or fast nav) can
// clobber the fresh mount's state or repaint into a torn-down host.
let mountGen = 0;

// --- fetch helpers ----------------------------------------------------------

async function getJson<T>(url: string): Promise<T> {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url} -> ${r.status}`);
  return (await r.json()) as T;
}

async function refreshRuns(): Promise<void> {
  const gen = mountGen;
  try {
    const { runs, disk } = await getJson<{ runs: RunSummary[]; disk: number }>("/api/runs");
    if (gen !== mountGen) return; // a later mount/unmount has already superseded this fetch
    state.runs = runs;
    state.disk = disk;
    paintRuns();
  } catch { /* transient; next poll retries */ }
}

async function refreshCi(): Promise<void> {
  const gen = mountGen;
  try {
    const ci = await getJson<CiPayload>("/api/ci");
    if (gen !== mountGen) return;
    state.ci = ci;
    paintCi();
  } catch { /* keep last-known CI */ }
}

async function refreshTemplates(): Promise<void> {
  const gen = mountGen;
  try {
    const { templates, stale } = await getJson<{ templates: Template[]; stale: StaleInfo[] }>("/api/control/templates");
    if (gen !== mountGen) return;
    state.templates = templates;
    state.stale = stale;
    paintForm();
  } catch { /* keep last-known template/stale info */ }
}

async function refreshCatalog(): Promise<void> {
  const gen = mountGen;
  try {
    const r = await fetch("/api/catalog");
    if (r.status === 500) {
      if (gen !== mountGen) return;
      state.catalogError = true;
      state.catalog = [];
      return;
    }
    const payload = (await r.json()) as { version: number; entries: CatalogEntry[] };
    if (gen !== mountGen) return;
    state.catalog = Array.isArray(payload.entries) ? payload.entries : [];
    state.catalogError = false;
  } catch {
    if (gen !== mountGen) return;
    state.catalogError = true;
    state.catalog = [];
  }
}

// --- section painting -------------------------------------------------------

/**
 * Writes `html` into the [data-fr="name"] section — UNLESS that section
 * currently contains document.activeElement, in which case the repaint is
 * deferred (stashed in `pending`) and applied by the focusout flush once the
 * user's focus leaves. This is what keeps an open <select> alive across polls.
 */
function paintSection(name: string, html: string): void {
  const el = host?.querySelector<HTMLElement>(`[data-fr="${name}"]`);
  if (!el) return;
  if (el.contains(document.activeElement)) { pending.set(name, html); return; }
  el.innerHTML = html;
}

/**
 * After focus leaves a deferred section, apply its stashed HTML on the next tick.
 * The form section carries user selection + chz-only visibility that the fresh
 * markup would reset, so we preserve them across the deferred write exactly as
 * the immediate paintForm path does (capture -> write -> restore -> sync).
 */
function flushPending(): void {
  if (pending.size === 0) return;
  const drained = [...pending.entries()];
  pending.clear();
  for (const [name, html] of drained) {
    if (name === "form") {
      const form = captureForm();
      paintSection("form", html);
      restoreForm(form);
      syncFormVisibility();
    } else if (name === "archive") {
      paintSection("archive", html);
      // The fresh archive markup has an empty drawer slot; re-attach the open
      // drawer so clicking away from a focused archive element doesn't blank it.
      if (state.openDetailId) void openDetail(state.openDetailId, isRunLive(state.openDetailId));
    } else {
      paintSection(name, html);
    }
  }
}

/** Active + archive sections both derive from state.runs, so they repaint together. */
function paintRuns(): void {
  paintSection("active", renderActive(state.runs, Date.now()));
  paintArchive();
}

function paintArchive(): void {
  paintSection("archive", renderArchive(state.runs, state.disk));
  // Re-attach the open detail drawer, if any. Finished runs are immutable —
  // only re-fetch when the run is still running/stalled (this used to re-invoke
  // a full detail fetch every 2s poll even for archived runs).
  if (state.openDetailId) void openDetail(state.openDetailId, isRunLive(state.openDetailId));
}

function paintCi(): void {
  paintSection("ci", renderCiStrip(state.ci));
}

/**
 * Repaints the form section — but only when its input ({templates, stale})
 * actually changed since the last render (guard (a)). In steady state this is a
 * no-op, so a user paging through the template <select> is never disturbed.
 * When it does repaint, we preserve the in-progress selection and re-sync the
 * chz-only field visibility (the fresh markup starts at the default option).
 */
function paintForm(): void {
  if (!host) return;
  const payload = JSON.stringify({ templates: state.templates, stale: state.stale });
  if (payload === lastFormPayload) return; // nothing changed -> leave the DOM (and any open select) alone
  lastFormPayload = payload;
  const form = captureForm();
  paintSection("form", renderForm(state.templates, state.stale));
  restoreForm(form);
  syncFormVisibility();
}

function paintCatalogBanner(): void {
  const el = host?.querySelector<HTMLElement>('[data-fr="catalog-banner"]');
  if (!el) return;
  el.innerHTML = state.catalogError
    ? `<p class="fr-catalog-warn">catalog unreadable — test explanations disabled</p>`
    : "";
}

// --- form helpers -----------------------------------------------------------

/** Reads the current new-run form selections so a form repaint can restore them. */
function captureForm(): { template?: string; model?: string; grid?: string } {
  if (!host) return {};
  return {
    template: host.querySelector<HTMLSelectElement>('[data-role="template"]')?.value,
    model: host.querySelector<HTMLSelectElement>('[data-role="model"]')?.value,
    grid: host.querySelector<HTMLSelectElement>('[data-role="grid"]')?.value,
  };
}

function restoreForm(f: { template?: string; model?: string; grid?: string }): void {
  if (!host) return;
  const set = (role: string, v?: string) => {
    if (v === undefined) return;
    const el = host!.querySelector<HTMLSelectElement>(`[data-role="${role}"]`);
    if (el) el.value = v;
  };
  set("template", f.template);
  set("model", f.model);
  set("grid", f.grid);
}

/** Shows/hides the model+grid selects based on whether the picked template is chz. */
function syncFormVisibility(): void {
  if (!host) return;
  const sel = host.querySelector<HTMLSelectElement>('[data-role="template"]');
  const isChz = sel?.selectedOptions[0]?.dataset.chz === "1";
  for (const el of host.querySelectorAll<HTMLElement>(".fr-chz-only")) {
    el.classList.toggle("fr-hidden", !isChz);
  }
  // Stale chip reflects the *selected* binary specifically.
  const bin = sel?.selectedOptions[0]?.dataset.bin ?? "";
  const chip = host.querySelector<HTMLElement>('[data-role="stale-chip"]');
  if (chip) {
    const selectedStale = state.stale.some((s) => s.binary === bin && s.stale);
    chip.classList.toggle("fr-hidden", !selectedStale);
    if (selectedStale) chip.textContent = "⚠ selected binary is stale — rebuild before trusting results";
  }
}

// --- detail drawer ----------------------------------------------------------

const LIVE_STATUS = new Set(["running", "stalled"]);

/** True while `id`'s last-known status (per state.runs) is still live. Unknown ids (e.g. pruned from the list) are treated as finished — never poll forever on a run we can no longer see. */
function isRunLive(id: string): boolean {
  return LIVE_STATUS.has(state.runs.find((r) => r.id === id)?.status ?? "");
}

/**
 * Opens (or repaints) the detail drawer for `id`.
 *
 * `refetch` distinguishes a user-initiated open (always fetch fresh) from a
 * repaint-driven re-attach (paintArchive calls this on every 2s poll to keep
 * the drawer present across the archive section's innerHTML replace). On
 * repaint, a finished run's detail can't change, so we reuse the cached detail
 * and skip the network round-trip entirely; a live run still re-fetches so
 * progress and eventual pass/fail keep updating.
 */
async function openDetail(id: string, refetch: boolean = true): Promise<void> {
  if (!host) return;
  const drawer = host.querySelector<HTMLElement>('[data-role="detail-drawer"]');
  if (!drawer) return;

  if (!refetch && state.openDetailData && state.openDetailData.id === id) {
    drawer.innerHTML = renderRunDetail(state.openDetailData, state.catalog);
    return;
  }

  const gen = mountGen;
  try {
    const detail = await getJson<RunDetail>(`/api/runs/${encodeURIComponent(id)}`);
    if (gen !== mountGen) return;
    state.openDetailData = detail;
    state.openDetailId = id;
    drawer.innerHTML = renderRunDetail(detail, state.catalog);
  } catch {
    if (gen !== mountGen) return;
    drawer.innerHTML = `<p class="fr-empty">Could not load run detail.</p>`;
  }
}

function closeDetail(): void {
  state.openDetailId = null;
  state.openDetailData = null;
  const drawer = host?.querySelector<HTMLElement>('[data-role="detail-drawer"]');
  if (drawer) drawer.innerHTML = "";
}

function setFormMsg(msg: string): void {
  const el = host?.querySelector<HTMLElement>('[data-role="form-msg"]');
  if (el) el.textContent = msg;
}

// --- control actions --------------------------------------------------------

async function postControl(path: string, body: unknown): Promise<{ ok: boolean; error?: string; pid?: number }> {
  const r = await fetch(path, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify(body),
  });
  return (await r.json()) as { ok: boolean; error?: string; pid?: number };
}

async function doStart(templateId: string, params: { model?: string; grid?: string }): Promise<void> {
  setFormMsg("starting…");
  try {
    const res = await postControl("/api/control/start", { templateId, params });
    setFormMsg(res.ok ? `started (pid ${res.pid})` : `start failed: ${res.error ?? "unknown"}`);
  } catch (e) {
    setFormMsg(`start failed: ${(e as Error).message}`);
  }
  // Refresh templates (stale info) and runs after any start.
  await Promise.all([refreshTemplates(), refreshRuns()]);
}

async function doStop(id: string): Promise<void> {
  try {
    const res = await postControl("/api/control/stop", { id });
    if (!res.ok) setFormMsg(`stop failed: ${res.error ?? "unknown"}`);
  } catch (e) {
    setFormMsg(`stop failed: ${(e as Error).message}`);
  }
  await Promise.all([refreshTemplates(), refreshRuns()]);
}

// --- event delegation -------------------------------------------------------

function onClick(ev: Event): void {
  const t = ev.target as HTMLElement;
  const actionEl = t.closest<HTMLElement>("[data-action]");
  const action = actionEl?.dataset.action;
  if (!action) return;

  if (action === "stop") {
    const id = actionEl!.closest<HTMLElement>("[data-run-id]")?.dataset.runId;
    if (id && confirm("Stop this run? The process will be terminated.")) void doStop(id);
    return;
  }
  if (action === "rerun") {
    ev.stopPropagation(); // don't also trigger the row's detail-open
    const raw = actionEl!.dataset.rerun;
    if (!raw) return;
    try {
      const { templateId, params } = JSON.parse(raw) as { templateId: string; params: { model?: string; grid?: string } };
      void doStart(templateId, params ?? {});
    } catch { setFormMsg("re-run payload was malformed"); }
    return;
  }
  if (action === "detail") {
    const id = actionEl!.dataset.runId;
    if (id) { if (state.openDetailId === id) closeDetail(); else void openDetail(id); }
    return;
  }
  if (action === "close-detail") {
    closeDetail();
    return;
  }
  // "start" is handled by the form submit; ignore its click here.
}

function onSubmit(ev: Event): void {
  const form = ev.target as HTMLElement;
  if (form.getAttribute("data-role") !== "newrun-form") return;
  ev.preventDefault();
  if (!host) return;
  const templateId = host.querySelector<HTMLSelectElement>('[data-role="template"]')?.value ?? "";
  const isChz = host.querySelector<HTMLSelectElement>('[data-role="template"]')?.selectedOptions[0]?.dataset.chz === "1";
  const params: { model?: string; grid?: string } = {};
  if (isChz) {
    params.model = host.querySelector<HTMLSelectElement>('[data-role="model"]')?.value;
    params.grid = host.querySelector<HTMLSelectElement>('[data-role="grid"]')?.value;
  }
  void doStart(templateId, params);
}

function onChange(ev: Event): void {
  const t = ev.target as HTMLElement;
  if (t.getAttribute("data-role") === "template") syncFormVisibility();
}

// A section that skipped its repaint because it held focus (an open <select>,
// a focused input) gets its stashed HTML applied once focus leaves. Deferred to
// a microtask/next tick so we never rip the DOM out from under the very event
// that is still propagating.
function onFocusOut(): void {
  queueMicrotask(flushPending);
}

// --- lifecycle --------------------------------------------------------------

/** The five persistent section containers, built once at mount. `catalog` stays empty until Task 5. */
const SECTION_SKELETON =
  `<div data-fr="catalog-banner"></div>` +
  `<div class="fr-root">` +
  `<section data-fr="active"></section>` +
  `<section data-fr="ci"></section>` +
  `<section data-fr="form"></section>` +
  `<section data-fr="catalog"></section>` +
  `<section data-fr="archive"></section>` +
  `</div>`;

export function mountFranklin(app: HTMLElement): void {
  mountGen++; // invalidate any in-flight fetches from a previous mount
  host = app;
  state = emptyState();
  pending.clear();
  lastFormPayload = null;
  // Build the persistent sections ONCE. Pollers only ever rewrite a section's
  // innerHTML from here on — the section elements themselves are never replaced,
  // so an open <select> inside one survives every poll.
  app.innerHTML = SECTION_SKELETON;
  paintSection("active", `<p class="fr-empty">Loading Franklin…</p>`);

  clickHandler = (ev) => onClick(ev);
  submitHandler = (ev) => onSubmit(ev);
  changeHandler = (ev) => onChange(ev);
  focusoutHandler = () => onFocusOut();
  app.addEventListener("click", clickHandler);
  app.addEventListener("submit", submitHandler);
  app.addEventListener("change", changeHandler);
  app.addEventListener("focusout", focusoutHandler);

  // One-shot: catalog + templates, then paint with runs/ci.
  void (async () => {
    await Promise.all([refreshCatalog(), refreshTemplates()]);
    if (host) paintCatalogBanner(); // reflect catalog availability once known
    await Promise.all([refreshRuns(), refreshCi()]);
  })();

  timers.push(setInterval(() => void refreshRuns(), RUNS_POLL_MS));
  timers.push(setInterval(() => void refreshCi(), CI_POLL_MS));
}

export function unmountFranklin(): void {
  mountGen++; // invalidate any fetches still in flight from this mount
  for (const t of timers) clearInterval(t);
  timers.length = 0;
  if (host && clickHandler) host.removeEventListener("click", clickHandler);
  if (host && submitHandler) host.removeEventListener("submit", submitHandler);
  if (host && changeHandler) host.removeEventListener("change", changeHandler);
  if (host && focusoutHandler) host.removeEventListener("focusout", focusoutHandler);
  clickHandler = submitHandler = changeHandler = focusoutHandler = null;
  pending.clear();
  host = null;
}
