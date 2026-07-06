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
import { renderActive, renderCiStrip, renderForm, renderArchive, renderRunDetail, renderCatalogSection, catalogCards } from "./franklinRender.ts";
import type { RunSummary, RunDetail } from "./franklinTypes.ts";
import type { CiPayload } from "../server/ci.ts";
import type { StaleInfo, Template } from "../server/control.ts";
import type { CatalogEntry } from "./franklinExplain.ts";

const RUNS_POLL_MS = 2_000;
const CI_POLL_MS = 10_000;
const CATALOG_SEARCH_DEBOUNCE_MS = 150;

interface FranklinState {
  runs: RunSummary[];
  disk: number;
  ci: CiPayload;
  stale: StaleInfo[];
  templates: Template[];
  catalog: CatalogEntry[];
  catalogError: boolean;
  /** Live catalog-browser search query (persisted across polls so the input value survives). */
  catalogQuery: string;
  /** Per-test last-result (key -> ok) from the newest archived suite run, or null if none yet. */
  catalogLatest: Map<string, boolean> | null;
  /** Whether the catalog section has been painted once (it renders at mount, not on the runs poll). */
  catalogMounted: boolean;
  openDetailId: string | null;
  /** Last-fetched detail for openDetailId, reused by repaint when the run is finished. */
  openDetailData: RunDetail | null;
}

const EMPTY_CI: CiPayload = { available: false, fetchedAt: 0, branches: [] };

function emptyState(): FranklinState {
  return {
    runs: [], disk: 0, ci: EMPTY_CI, stale: [], templates: [], catalog: [], catalogError: false,
    catalogQuery: "", catalogLatest: null, catalogMounted: false,
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
let inputHandler: ((ev: Event) => void) | null = null;

// Debounce handle for the catalog search box: an input event re-renders ONLY the
// nested results container after this idle window, never repainting the <input>
// the user is typing into.
let catalogSearchTimer: ReturnType<typeof setTimeout> | null = null;

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
    void refreshCatalogLatest(); // update the browser's "Last result" column if a new suite finished
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

const FINISHED_SUITE = (r: RunSummary): boolean =>
  r.kind === "suite" && !LIVE_STATUS.has(r.status);

/**
 * Computes the catalog browser's "Last result" column: the newest *archived*
 * suite run's per-test ok flags, keyed by the catalog key (`${name} / ${sub}`).
 * Only refetches when the newest finished suite run's id changed since last time
 * (finished runs are immutable), so this is a no-op on most 2s polls. When no
 * finished suite run exists, latest stays null and every card reads
 * "never recorded".
 */
let lastLatestSuiteId: string | null = null;
async function refreshCatalogLatest(): Promise<void> {
  const newest = state.runs
    .filter(FINISHED_SUITE)
    .sort((a, b) => b.startedAt - a.startedAt)[0];
  if (!newest) { lastLatestSuiteId = null; return; }
  if (newest.id === lastLatestSuiteId && state.catalogLatest) return; // unchanged -> keep cached map
  const gen = mountGen;
  try {
    const detail = await getJson<RunDetail>(`/api/runs/${encodeURIComponent(newest.id)}`);
    if (gen !== mountGen) return;
    const map = new Map<string, boolean>();
    for (const t of detail.testsList) map.set(`${t.name} / ${t.sub}`, t.ok);
    state.catalogLatest = map;
    lastLatestSuiteId = newest.id;
    repaintCatalogResults(); // refresh the "Last result" column in place
  } catch { /* transient; the cached map (or null) stands until the next poll */ }
}

// --- section painting -------------------------------------------------------

/**
 * Writes `html` into the [data-fr="name"] section — UNLESS that section
 * currently contains document.activeElement, in which case the repaint is
 * deferred (stashed in `pending`) and applied by the focusout flush once the
 * user's focus leaves. This is what keeps an open <select> alive across polls.
 */
function paintSection(name: string, html: string): boolean {
  const el = host?.querySelector<HTMLElement>(`[data-fr="${name}"]`);
  if (!el) return false;
  if (el.contains(document.activeElement)) { pending.set(name, html); return false; }
  el.innerHTML = html;
  return true;
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
    } else if (name === "catalog") {
      // A deferred first catalog paint finally lands here — only now is the
      // results container in the DOM, so flip the sentinel (carried note #2).
      if (paintSection("catalog", html)) state.catalogMounted = true;
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

/**
 * Paints the whole catalog section ONCE (the catalog is fetched once at mount).
 * We route through paintSection so it inherits the focus-guard: if the user is
 * mid-search when a rare full repaint lands, it defers rather than ripping the
 * input out. `catalogMounted` guards against re-rendering the whole section
 * (and thus resetting the <details> open state + search box) on later polls —
 * only repaintCatalogResults touches the DOM after this.
 */
function paintCatalog(): void {
  if (!host) return;
  // Carried wiring note #2: flip the mounted sentinel only after a *non-deferred*
  // paint. If the section held focus and the write was stashed, the focusout
  // flush sets the sentinel instead (see flushPending) — so repaintCatalogResults
  // never runs against a container that isn't in the DOM yet.
  const painted = paintSection("catalog", renderCatalogSection(state.catalog, state.catalogQuery, state.catalogLatest));
  if (painted) state.catalogMounted = true;
}

/**
 * Re-renders ONLY the nested results container (`data-role="catalog-results"`) —
 * never the <input> the user is typing into (carried wiring note #1). Used by
 * the debounced search handler and by refreshCatalogLatest when the "Last result"
 * column changes. No-op until the catalog section has been mounted.
 */
function repaintCatalogResults(): void {
  if (!host || !state.catalogMounted) return;
  const results = host.querySelector<HTMLElement>('[data-role="catalog-results"]');
  if (!results) return;
  // Write ONLY the card list into the existing results div; the sibling search
  // <input> is never touched, so the caret/focus stay put mid-type.
  results.innerHTML = catalogCards(state.catalog, state.catalogQuery, state.catalogLatest);
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

/**
 * Catalog search: on each keystroke, stash the query and (debounced) re-render
 * ONLY the results list. The <input> itself is left untouched so the caret and
 * focus never jump mid-type (carried wiring note #1). We read the value eagerly
 * (before the timer) so a fast unmount can't strand a stale closure.
 */
function onInput(ev: Event): void {
  const t = ev.target as HTMLElement;
  if (t.getAttribute("data-role") !== "catalog-search") return;
  state.catalogQuery = (t as HTMLInputElement).value;
  if (catalogSearchTimer) clearTimeout(catalogSearchTimer);
  catalogSearchTimer = setTimeout(() => {
    catalogSearchTimer = null;
    repaintCatalogResults();
  }, CATALOG_SEARCH_DEBOUNCE_MS);
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
  `<div class="fr-main">` +
  `<section data-fr="active"></section>` +
  `<section data-fr="archive"></section>` +
  `</div>` +
  `<div class="fr-side">` +
  `<section data-fr="ci"></section>` +
  `<section data-fr="form"></section>` +
  `</div>` +
  `<section data-fr="catalog"></section>` +
  `</div>`;

export function mountFranklin(app: HTMLElement): void {
  mountGen++; // invalidate any in-flight fetches from a previous mount
  host = app;
  state = emptyState();
  pending.clear();
  lastFormPayload = null;
  lastLatestSuiteId = null;
  if (catalogSearchTimer) { clearTimeout(catalogSearchTimer); catalogSearchTimer = null; }
  // Build the persistent sections ONCE. Pollers only ever rewrite a section's
  // innerHTML from here on — the section elements themselves are never replaced,
  // so an open <select> inside one survives every poll.
  app.innerHTML = SECTION_SKELETON;
  paintSection("active", `<p class="fr-empty">Loading Franklin…</p>`);

  clickHandler = (ev) => onClick(ev);
  submitHandler = (ev) => onSubmit(ev);
  changeHandler = (ev) => onChange(ev);
  focusoutHandler = () => onFocusOut();
  inputHandler = (ev) => onInput(ev);
  app.addEventListener("click", clickHandler);
  app.addEventListener("submit", submitHandler);
  app.addEventListener("change", changeHandler);
  app.addEventListener("focusout", focusoutHandler);
  app.addEventListener("input", inputHandler);

  // One-shot: catalog + templates, then paint with runs/ci. The catalog section
  // is painted once here (fetched once); later polls only touch its results list.
  void (async () => {
    await Promise.all([refreshCatalog(), refreshTemplates()]);
    if (host) { paintCatalogBanner(); paintCatalog(); } // reflect catalog availability + render browser once
    await Promise.all([refreshRuns(), refreshCi()]);
  })();

  timers.push(setInterval(() => void refreshRuns(), RUNS_POLL_MS));
  timers.push(setInterval(() => void refreshCi(), CI_POLL_MS));
}

export function unmountFranklin(): void {
  mountGen++; // invalidate any fetches still in flight from this mount
  for (const t of timers) clearInterval(t);
  timers.length = 0;
  if (catalogSearchTimer) { clearTimeout(catalogSearchTimer); catalogSearchTimer = null; }
  lastLatestSuiteId = null;
  if (host && clickHandler) host.removeEventListener("click", clickHandler);
  if (host && submitHandler) host.removeEventListener("submit", submitHandler);
  if (host && changeHandler) host.removeEventListener("change", changeHandler);
  if (host && focusoutHandler) host.removeEventListener("focusout", focusoutHandler);
  if (host && inputHandler) host.removeEventListener("input", inputHandler);
  clickHandler = submitHandler = changeHandler = focusoutHandler = inputHandler = null;
  pending.clear();
  host = null;
}
