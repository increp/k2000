// Franklin dashboard — pure string-building renderers (no I/O, no DOM).
// Mirrors src/render.ts's idioms: template-literal HTML, every user-derived
// string escaped through esc(), event wiring via data-action attributes.
//
// Type-only imports for the server modules: erased at bundle time so no
// node:* code leaks into the browser bundle (see task brief).
import type { RunSummary, RunDetail, RunCheck, TestEvent } from "./franklinTypes.ts";
import type { CiPayload } from "../server/ci.ts";
import type { StaleInfo, Template } from "../server/control.ts";
import { catalogLookup, explainChzLabel } from "./franklinExplain.ts";
import type { CatalogEntry } from "./franklinExplain.ts";

/** HTML-escape any user-derived string. Same four substitutions as render.ts. */
export function esc(s: string): string {
  return s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

/** Escapes a string for safe embedding inside an HTML attribute (adds single-quote). */
function escAttr(s: string): string {
  return esc(s).replace(/'/g, "&#39;");
}

/** Rounds to one decimal and drops a trailing ".0" ("16.6", "45", "2.5"). */
function trim1(n: number): string {
  const r = Math.round(n * 10) / 10;
  return Number.isInteger(r) ? String(r) : String(r);
}

/** Humanizes a millisecond span as "16.6 s" / "2.5 m" / "3.1 h". */
export function humanizeMs(ms: number): string {
  if (!Number.isFinite(ms) || ms < 0) return "—";
  const s = ms / 1000;
  if (s < 60) return `${trim1(s)} s`;
  const m = s / 60;
  if (m < 60) return `${trim1(m)} m`;
  const h = m / 60;
  return `${trim1(h)} h`;
}

/** Humanizes a seconds span (archive durations arrive as durationS). */
function humanizeS(sec: number): string {
  return humanizeMs(sec * 1000);
}

/** Bytes -> "X.Y MB" (decimal MB, one decimal). */
function mb(bytes: number): string {
  return `${(bytes / 1_000_000).toFixed(1)} MB`;
}

function pct(done: number, total: number): number {
  if (total <= 0) return 0;
  return Math.round((done / total) * 100);
}

// ---------------------------------------------------------------------------
// Active run cards
// ---------------------------------------------------------------------------

const RUNNING = new Set(["running", "stalled"]);

function isActive(r: RunSummary): boolean {
  return RUNNING.has(r.status);
}

/** ETA text for a running chz card. Guards done>0 so we never divide by zero. */
function etaText(r: RunSummary, now: number): string {
  const done = r.done ?? 0;
  const total = r.total ?? 0;
  if (done <= 0 || total <= 0 || total <= done) return "";
  const elapsed = now - r.startedAt;
  if (!Number.isFinite(elapsed) || elapsed <= 0) return "";
  const remaining = (elapsed / done) * (total - done);
  if (!Number.isFinite(remaining) || remaining <= 0) return "";
  return `ETA ${humanizeMs(remaining)}`;
}

function activeCard(r: RunSummary, now: number): string {
  const done = r.done ?? 0;
  const total = r.total ?? 0;
  const p = pct(done, total);
  const counter = total > 0 ? `${done} / ${total}` : `${done}`;
  const eta = etaText(r, now);

  // Explained operating point (chz only; suite has no chz labels).
  let labelBlock = "";
  if (r.kind === "chz" && r.label) {
    const { title } = explainChzLabel(r.label);
    labelBlock =
      `<div class="fr-card-label"><span class="fr-label-title">${esc(title)}</span>` +
      `<span class="fr-label-raw">${esc(r.label)}</span></div>`;
  } else if (r.label) {
    labelBlock = `<div class="fr-card-label"><span class="fr-label-raw">${esc(r.label)}</span></div>`;
  }

  const meta = r.kind === "chz"
    ? `${esc(r.model ?? "all")} · ${esc(r.grid ?? "full")}`
    : "suite";

  return `
    <div class="fr-card status-${esc(r.status)}" data-run-id="${escAttr(r.id)}">
      <div class="fr-card-head">
        <span class="fr-kind">${esc(r.kind)}</span>
        <span class="fr-card-meta">${meta}</span>
        <span class="status-badge">${esc(r.status)}</span>
        <span class="fr-spacer"></span>
        <span class="fr-counter">${esc(counter)}</span>
        <span class="fr-eta">${esc(eta)}</span>
        <button data-action="stop" title="Stop this run">stop</button>
      </div>
      <div class="fr-progress" title="${p}%"><div class="fr-bar" style="width: ${p}%"></div></div>
      ${labelBlock}
    </div>`;
}

/** Active-runs section (running/stalled cards). One of the five persistent sections. */
export function renderActive(runs: RunSummary[], now: number): string {
  const active = runs.filter(isActive);
  const body = active.length > 0
    ? active.map((r) => activeCard(r, now)).join("")
    : `<p class="fr-empty">No active runs.</p>`;
  return `<section class="fr-section fr-active">
      <h2>Active runs</h2>
      ${body}
    </section>`;
}

// ---------------------------------------------------------------------------
// CI strip
// ---------------------------------------------------------------------------

// Only http(s) URLs are safe to render as a clickable href — gh output is
// trusted today, but a hostile/malformed url (e.g. "javascript:alert(1)")
// must never survive into an anchor just because escAttr() escaped its quotes.
const SAFE_URL = /^https?:\/\//i;

function ciCheckDot(c: CiPayload["branches"][number]["checks"][number]): string {
  // Map gh status/conclusion to a verdict class for coloring.
  let cls = "info";
  if (c.status !== "completed") cls = "running";
  else if (c.conclusion === "success") cls = "pass";
  else if (c.conclusion === "failure" || c.conclusion === "timed_out" || c.conclusion === "cancelled") cls = "fail";
  const label = c.conclusion ?? c.status;
  const inner = `<span class="fr-ci-check fr-ci-${cls}">${esc(c.name)}: ${esc(label)}</span>`;
  return c.url && SAFE_URL.test(c.url)
    ? `<a class="fr-ci-link" href="${escAttr(c.url)}" target="_blank" rel="noopener">${inner}</a>`
    : inner;
}

/** CI strip section (per-branch check dots). One of the five persistent sections. */
export function renderCiStrip(ci: CiPayload): string {
  if (!ci.available) {
    return `<section class="fr-section fr-ci">
        <h2>CI</h2>
        <p class="fr-empty">CI status unavailable (gh not reachable).</p>
      </section>`;
  }
  const branches = ci.branches.map((b) => {
    const checks = b.checks.length > 0
      ? b.checks.map(ciCheckDot).join("")
      : `<span class="fr-empty">no checks</span>`;
    return `<div class="fr-ci-branch">
        <span class="fr-ci-ref">${esc(b.title || b.ref)}</span>
        <span class="fr-ci-checks">${checks}</span>
      </div>`;
  }).join("");
  return `<section class="fr-section fr-ci">
      <h2>CI</h2>
      ${branches || `<p class="fr-empty">No open PRs or recent runs.</p>`}
    </section>`;
}

// ---------------------------------------------------------------------------
// New-run form
// ---------------------------------------------------------------------------

// The chz binary path (control.ts). A template drives chz iff its binary is this
// one — the model/grid selects only apply to chz. We key the stale chip off the
// PATH of the binary each template drives — never a positional index, since the
// four templates share only two binaries.
const CHZ_BIN = "build/tests/k2000_device_characterization";

function isChzTemplate(t: Template): boolean {
  return t.bin === CHZ_BIN;
}

function staleFor(bin: string, stale: StaleInfo[]): boolean {
  return stale.some((s) => s.binary === bin && s.stale);
}

/**
 * New-run form section (template picker + chz model/grid + stale chip).
 * One of the five persistent sections. Takes the real Template[] the server
 * ships alongside stale info; `data-chz`/`data-bin` on each option let the
 * client toggle the chz-only fields and target the stale chip by binary path.
 */
export function renderForm(templates: Template[], stale: StaleInfo[]): string {
  const options = templates.map((t) => {
    const chz = isChzTemplate(t);
    const chip = staleFor(t.bin, stale) ? " ⚠ stale" : "";
    return `<option value="${escAttr(t.id)}" data-chz="${chz ? "1" : "0"}" data-bin="${escAttr(t.bin)}">${esc(t.label)}${chip}</option>`;
  }).join("");

  // Any stale binary at all -> a visible top-level chip (per-template chip is in
  // the option text; this one is the always-visible summary the tests key on).
  const anyStale = templates.some((t) => staleFor(t.bin, stale));
  const staleChip = anyStale
    ? `<span class="fr-stale-chip" data-role="stale-chip">⚠ a selected binary may be stale — rebuild before trusting results</span>`
    : `<span class="fr-stale-chip fr-hidden" data-role="stale-chip"></span>`;

  return `<section class="fr-section fr-newrun">
      <h2>New run</h2>
      <form data-role="newrun-form" class="fr-form">
        <label>Template
          <select data-role="template">${options}</select>
        </label>
        <label class="fr-chz-only" data-role="model-wrap">Model
          <select data-role="model">
            <option value="all">all</option>
            <option value="moog">moog</option>
            <option value="huggett">huggett</option>
          </select>
        </label>
        <label class="fr-chz-only" data-role="grid-wrap">Grid
          <select data-role="grid">
            <option value="full">full</option>
            <option value="quick">quick</option>
          </select>
        </label>
        <button type="submit" data-action="start">Start</button>
        ${staleChip}
      </form>
      <p class="fr-form-msg" data-role="form-msg"></p>
    </section>`;
}

// ---------------------------------------------------------------------------
// Archive table
// ---------------------------------------------------------------------------

/** The re-run payload for a finished row. chz carries model/grid; suite is always plain. */
function rerunPayload(r: RunSummary): string {
  if (r.kind === "chz") {
    return JSON.stringify({ templateId: "chz", params: { model: r.model ?? "all", grid: r.grid ?? "full" } });
  }
  return JSON.stringify({ templateId: "suite", params: {} });
}

function archiveRow(r: RunSummary): string {
  const when = new Date(r.startedAt).toISOString().replace("T", " ").slice(0, 19);
  const dur = r.durationS !== undefined ? humanizeS(r.durationS) : "—";
  const desc = r.kind === "chz"
    ? `chz ${esc(r.model ?? "all")}/${esc(r.grid ?? "full")}`
    : "suite";
  const counts = r.kind === "suite" && r.tests !== undefined
    ? `${r.tests - (r.failed ?? 0)}/${r.tests} ok`
    : "";
  const rerunLabel = r.kind === "suite" ? "re-run (plain)" : "re-run";
  return `<tr class="fr-arch-row status-${esc(r.status)}" data-run-id="${escAttr(r.id)}" data-action="detail">
      <td class="fr-arch-when">${esc(when)}</td>
      <td class="fr-arch-desc">${desc}</td>
      <td><span class="status-badge">${esc(r.status)}</span></td>
      <td class="fr-arch-dur">${esc(dur)}</td>
      <td class="fr-arch-counts">${esc(counts)}</td>
      <td class="fr-arch-size">${esc(mb(r.sizeBytes))}</td>
      <td><button data-action="rerun" data-rerun='${escAttr(rerunPayload(r))}'>${esc(rerunLabel)}</button></td>
    </tr>`;
}

/**
 * Archive section (finished-run table + detail-drawer mount point).
 * One of the five persistent sections. The drawer (`data-role="detail-drawer"`)
 * is filled by franklin.ts's openDetail; renderArchive only lays out its slot.
 */
export function renderArchive(runs: RunSummary[], diskBytes: number): string {
  const finished = runs.filter((r) => !isActive(r));
  const rows = finished.length > 0
    ? finished.map(archiveRow).join("")
    : `<tr><td colspan="7" class="fr-empty">No archived runs.</td></tr>`;
  return `<section class="fr-section fr-archive">
      <h2>Archive <span class="fr-disk">disk: ${esc(mb(diskBytes))}</span></h2>
      <table class="fr-arch-table">
        <thead><tr><th>started</th><th>run</th><th>status</th><th>dur</th><th>tests</th><th>size</th><th></th></tr></thead>
        <tbody>${rows}</tbody>
      </table>
      <div data-role="detail-drawer" class="fr-drawer"></div>
    </section>`;
}

// ---------------------------------------------------------------------------
// deviationRows — the deviations-first ranking (spec §8)
// ---------------------------------------------------------------------------

export interface DeviationRow {
  severity: 0 | 1 | 2;
  title: string;
  detail: string;
  /** |measured - expected| when both present, else undefined (sorts last within severity). */
  delta?: number;
}

// Named checks always surfaced as margins even when passing / without an expected.
const ALWAYS_MARGIN = ["method_delta_db", "selfosc_cents_err", "noise_floor_dbfs"];

function alwaysMargin(name: string): boolean {
  return ALWAYS_MARGIN.some((k) => name.includes(k));
}

/**
 * Ranks a run's anomalies deviations-first:
 *  sev 0 = each failing-test message + each check verdict "fail"
 *  sev 1 = run-state anomalies (stalled / error / stopped)
 *  sev 2 = margin rows: any check with an expected (measured/expected/delta),
 *          plus the three always-show named margins even when passing.
 * Sorted: severity asc, then |delta| desc (rows without a delta last).
 */
export function deviationRows(d: RunDetail): DeviationRow[] {
  const rows: DeviationRow[] = [];

  // severity 0 — failing tests (one row per message) + failing checks
  for (const t of d.testsList) {
    if (!t.ok) {
      const where = `${t.name} / ${t.sub}`;
      if (t.messages.length > 0) {
        for (const m of t.messages) rows.push({ severity: 0, title: where, detail: m });
      } else {
        rows.push({ severity: 0, title: where, detail: `${t.failures} failure(s)` });
      }
    }
  }
  for (const c of d.checks) {
    if (c.verdict === "fail") {
      rows.push({ severity: 0, title: c.name, detail: marginDetail(c), delta: deltaOf(c) });
    }
  }

  // severity 1 — run-state anomalies
  if (d.status === "stalled") {
    rows.push({ severity: 1, title: "Run stalled", detail: "No progress within the stall window; the process may be hung." });
  } else if (d.status === "error") {
    rows.push({ severity: 1, title: "Run errored", detail: "The run terminated with an error outcome." });
  } else if (d.status === "stopped") {
    rows.push({ severity: 1, title: "Run stopped", detail: "The run was stopped before completing." });
  }

  // severity 2 — margins (skip checks already emitted as sev-0 fails)
  for (const c of d.checks) {
    if (c.verdict === "fail") continue;
    const hasExpected = c.expected !== undefined;
    if (hasExpected || alwaysMargin(c.name)) {
      rows.push({ severity: 2, title: c.name, detail: marginDetail(c), delta: deltaOf(c) });
    }
  }

  rows.sort((a, b) => {
    if (a.severity !== b.severity) return a.severity - b.severity;
    const da = a.delta ?? -Infinity;
    const db = b.delta ?? -Infinity;
    return db - da; // larger |delta| first; undefined (-Inf) sinks to the end
  });

  return rows;
}

function deltaOf(c: RunCheck): number | undefined {
  return c.expected !== undefined ? Math.abs(c.measured - c.expected) : undefined;
}

function marginDetail(c: RunCheck): string {
  if (c.expected !== undefined) {
    const delta = c.measured - c.expected;
    const sign = delta >= 0 ? "+" : "";
    return `measured ${fmtNum(c.measured)} vs expected ${fmtNum(c.expected)} (Δ ${sign}${fmtNum(delta)})`;
  }
  return `measured ${fmtNum(c.measured)}`;
}

function fmtNum(n: number): string {
  if (!Number.isFinite(n)) return String(n);
  // Trim to a reasonable precision without trailing-zero noise.
  return Number.isInteger(n) ? String(n) : String(Math.round(n * 1000) / 1000);
}

// ---------------------------------------------------------------------------
// renderRunDetail — deviations first, then tests, then chz label + metadata
// ---------------------------------------------------------------------------

function deviationsPanel(d: RunDetail): string {
  const rows = deviationRows(d);
  if (rows.length === 0) {
    return `<div class="fr-deviations"><h3>Deviations</h3><p class="fr-empty">No deviations — clean run.</p></div>`;
  }
  const body = rows.map((r) => {
    const sevClass = r.severity === 0 ? "fr-dev-crit" : r.severity === 1 ? "fr-dev-warn" : "fr-dev-info";
    return `<div class="fr-dev-row ${sevClass}">
        <span class="fr-dev-sev">S${r.severity}</span>
        <span class="fr-dev-title">${esc(r.title)}</span>
        <span class="fr-dev-detail">${esc(r.detail)}</span>
      </div>`;
  }).join("");
  return `<div class="fr-deviations"><h3>Deviations</h3>${body}</div>`;
}

function testRow(t: TestEvent, catalog: CatalogEntry[]): string {
  const entry = catalogLookup(catalog, t.name, t.sub);
  const prose = entry
    ? `<div class="fr-test-prose">
         <div class="fr-prose-what">${esc(entry.what)}</div>
         <div class="fr-prose-why">${esc(entry.why)}</div>
         <div class="fr-prose-dev"><em>If this fails:</em> ${esc(entry.deviationMeans)}</div>
         ${entry.file ? `<div class="fr-prose-file">${esc(entry.file)}</div>` : ""}
       </div>`
    : "";
  // Only failing tests dump their messages, and they get the red highlight.
  const messages = (!t.ok && t.messages.length > 0)
    ? `<ul class="fr-msg-list">` +
      t.messages.map((m) => `<li class="deviation-fail fr-msg-fail">${esc(m)}</li>`).join("") +
      `</ul>`
    : "";
  const okClass = t.ok ? "fr-test-ok" : "fr-test-fail";
  return `<details class="fr-test ${okClass}"${t.ok ? "" : " open"}>
      <summary><span class="status-badge">${t.ok ? "pass" : "fail"}</span> ${esc(t.name)} / ${esc(t.sub)}
        <span class="fr-test-counts">${t.passes}✓ ${t.failures}✗</span></summary>
      ${messages}
      ${prose}
    </details>`;
}

function testsPanel(d: RunDetail, catalog: CatalogEntry[]): string {
  if (d.testsList.length === 0) {
    return `<div class="fr-tests"><h3>Tests</h3><p class="fr-empty">No test events recorded.</p></div>`;
  }
  // Failing tests first for scannability.
  const ordered = [...d.testsList].sort((a, b) => Number(a.ok) - Number(b.ok));
  const body = ordered.map((t) => testRow(t, catalog)).join("");
  return `<div class="fr-tests"><h3>Tests</h3>${body}</div>`;
}

function chzLabelPanel(d: RunDetail): string {
  if (d.kind !== "chz") return "";
  const last = d.progressTail.at(-1);
  if (!last?.label) return "";
  const { title, body } = explainChzLabel(last.label);
  return `<div class="fr-chz-explain">
      <h3>Operating point</h3>
      <div class="fr-label-title">${esc(title)}</div>
      <p class="fr-label-body">${esc(body)}</p>
      <div class="fr-label-raw">${esc(last.label)}</div>
    </div>`;
}

function metadataPanel(d: RunDetail): string {
  const argv = d.argv?.length ? esc(d.argv.join(" ")) : "—";
  const sha = d.gitSha ? esc(d.gitSha.slice(0, 12)) : "—";
  const build = d.buildType ? esc(d.buildType) : "—";
  const csv = d.kind === "chz"
    ? `<div class="fr-meta-row"><span class="fr-meta-k">CSV</span><span class="fr-meta-v">build/characterization/${esc(d.model ?? "all")}/</span></div>`
    : "";
  return `<div class="fr-meta">
      <h3>Metadata</h3>
      <div class="fr-meta-row"><span class="fr-meta-k">buildType</span><span class="fr-meta-v">${build}</span></div>
      <div class="fr-meta-row"><span class="fr-meta-k">gitSha</span><span class="fr-meta-v">${sha}</span></div>
      <div class="fr-meta-row"><span class="fr-meta-k">argv</span><span class="fr-meta-v">${argv}</span></div>
      ${csv}
    </div>`;
}

export function renderRunDetail(d: RunDetail, catalog: CatalogEntry[]): string {
  return `<div class="fr-detail" data-run-id="${escAttr(d.id)}">
      <div class="fr-detail-head">
        <span class="fr-kind">${esc(d.kind)}</span>
        <span class="status-badge">${esc(d.status)}</span>
        <span class="fr-spacer"></span>
        <button data-action="close-detail" title="Close">✕</button>
      </div>
      ${deviationsPanel(d)}
      ${testsPanel(d, catalog)}
      ${chzLabelPanel(d)}
      ${metadataPanel(d)}
    </div>`;
}
