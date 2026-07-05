// Franklin dashboard — the live view controller. Fetches run/CI/catalog/template
// state, polls on timers, delegates all UI events by data-action (main.ts idiom),
// and hands the string HTML from franklinRender.ts to the DOM.
//
// Type-only imports for server modules (erased at bundle time — no node:* leaks).
import { renderRunsPage, renderRunDetail } from "./franklinRender.ts";
import type { RunSummary, RunDetail } from "./franklinTypes.ts";
import type { CiPayload } from "../server/ci.ts";
import type { StaleInfo } from "../server/control.ts";
import type { CatalogEntry } from "./franklinExplain.ts";

const RUNS_POLL_MS = 2_000;
const CI_POLL_MS = 10_000;

interface FranklinState {
  runs: RunSummary[];
  disk: number;
  ci: CiPayload;
  stale: StaleInfo[];
  catalog: CatalogEntry[];
  catalogError: boolean;
  openDetailId: string | null;
}

const EMPTY_CI: CiPayload = { available: false, fetchedAt: 0, branches: [] };

let host: HTMLElement | null = null;
let state: FranklinState = {
  runs: [], disk: 0, ci: EMPTY_CI, stale: [], catalog: [], catalogError: false, openDetailId: null,
};
const timers: ReturnType<typeof setInterval>[] = [];
let clickHandler: ((ev: Event) => void) | null = null;
let submitHandler: ((ev: Event) => void) | null = null;
let changeHandler: ((ev: Event) => void) | null = null;

// --- fetch helpers ----------------------------------------------------------

async function getJson<T>(url: string): Promise<T> {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url} -> ${r.status}`);
  return (await r.json()) as T;
}

async function refreshRuns(): Promise<void> {
  try {
    const { runs, disk } = await getJson<{ runs: RunSummary[]; disk: number }>("/api/runs");
    state.runs = runs;
    state.disk = disk;
    paint();
  } catch { /* transient; next poll retries */ }
}

async function refreshCi(): Promise<void> {
  try {
    state.ci = await getJson<CiPayload>("/api/ci");
    paint();
  } catch { /* keep last-known CI */ }
}

async function refreshTemplates(): Promise<void> {
  try {
    const { stale } = await getJson<{ stale: StaleInfo[] }>("/api/control/templates");
    state.stale = stale;
    paint();
  } catch { /* keep last-known stale info */ }
}

async function refreshCatalog(): Promise<void> {
  try {
    const r = await fetch("/api/catalog");
    if (r.status === 500) { state.catalogError = true; state.catalog = []; return; }
    const payload = (await r.json()) as { version: number; entries: CatalogEntry[] };
    state.catalog = Array.isArray(payload.entries) ? payload.entries : [];
    state.catalogError = false;
  } catch {
    state.catalogError = true;
    state.catalog = [];
  }
}

// --- render -----------------------------------------------------------------

/** Reads the current new-run form selections so a poll repaint can restore them. */
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

function paint(): void {
  if (!host) return;
  // A 2s poll fully re-renders the page; capture the user's in-progress form
  // selections first and restore them so a poll never clobbers a half-filled form.
  const form = captureForm();
  const banner = state.catalogError
    ? `<p class="fr-catalog-warn">catalog unreadable — test explanations disabled</p>`
    : "";
  host.innerHTML = banner + renderRunsPage(state.runs, state.ci, state.disk, state.stale);
  restoreForm(form);
  // Reflect chz-only field visibility for the current template selection.
  syncFormVisibility();
  // Re-attach the open detail drawer, if any.
  if (state.openDetailId) void openDetail(state.openDetailId);
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

async function openDetail(id: string): Promise<void> {
  if (!host) return;
  const drawer = host.querySelector<HTMLElement>('[data-role="detail-drawer"]');
  if (!drawer) return;
  try {
    const detail = await getJson<RunDetail>(`/api/runs/${encodeURIComponent(id)}`);
    drawer.innerHTML = renderRunDetail(detail, state.catalog);
    state.openDetailId = id;
  } catch {
    drawer.innerHTML = `<p class="fr-empty">Could not load run detail.</p>`;
  }
}

function closeDetail(): void {
  state.openDetailId = null;
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

// --- lifecycle --------------------------------------------------------------

export function mountFranklin(app: HTMLElement): void {
  host = app;
  state = { runs: [], disk: 0, ci: EMPTY_CI, stale: [], catalog: [], catalogError: false, openDetailId: null };
  app.innerHTML = `<p class="fr-empty">Loading Franklin…</p>`;

  clickHandler = (ev) => onClick(ev);
  submitHandler = (ev) => onSubmit(ev);
  changeHandler = (ev) => onChange(ev);
  app.addEventListener("click", clickHandler);
  app.addEventListener("submit", submitHandler);
  app.addEventListener("change", changeHandler);

  // One-shot: catalog + templates, then paint with runs/ci.
  void (async () => {
    await Promise.all([refreshCatalog(), refreshTemplates()]);
    await Promise.all([refreshRuns(), refreshCi()]);
  })();

  timers.push(setInterval(() => void refreshRuns(), RUNS_POLL_MS));
  timers.push(setInterval(() => void refreshCi(), CI_POLL_MS));
}

export function unmountFranklin(): void {
  for (const t of timers) clearInterval(t);
  timers.length = 0;
  if (host && clickHandler) host.removeEventListener("click", clickHandler);
  if (host && submitHandler) host.removeEventListener("submit", submitHandler);
  if (host && changeHandler) host.removeEventListener("change", changeHandler);
  clickHandler = submitHandler = changeHandler = null;
  host = null;
}
