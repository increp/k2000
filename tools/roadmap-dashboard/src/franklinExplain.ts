// Franklin dashboard: catalog lookup + generated battery prose.
//
// catalogLookup answers "what does this test mean" (docs/franklin/test-catalog.json,
// built by Task 4). explainChzLabel answers "what does this chz progress-line operating
// point mean" — it parses the label strings the C++ characterization runner emits
// (tests/characterization/characterize_main.cpp + CharacterizationRunner.cpp) into
// human-readable prose, one of four fixed battery templates (B1..B4).

export interface CatalogEntry {
  key: string;
  file: string;
  what: string;
  why: string;
  deviationMeans: string;
  links: string[];
  /** v2 (Task 3): the concrete quantities the test compares. Absent on stale v1 catalogs. */
  compares?: string;
  /** v2 (Task 3): what a passing result actually proves. Absent on stale v1 catalogs. */
  succeedingMeans?: string;
}

/** Looks up a catalog entry by `${name} / ${sub}` (exact key match). Null on miss. */
export function catalogLookup(entries: CatalogEntry[], name: string, sub: string): CatalogEntry | null {
  const key = `${name} / ${sub}`;
  return entries.find((e) => e.key === key) ?? null;
}

/**
 * Case-insensitive substring filter over key + file + all prose fields
 * (what/why/deviationMeans/compares/succeedingMeans). An empty or whitespace-only
 * query returns every entry (same array, unfiltered). v1 entries that lack the
 * v2 prose fields are tolerated — a missing field simply contributes nothing to
 * the haystack rather than throwing on `undefined`.
 */
export function filterCatalog(entries: CatalogEntry[], query: string): CatalogEntry[] {
  const q = query.trim().toLowerCase();
  if (q === "") return entries;
  return entries.filter((e) => {
    const hay = [e.key, e.file, e.what, e.why, e.deviationMeans, e.compares, e.succeedingMeans]
      .filter((s): s is string => typeof s === "string")
      .join("\n")
      .toLowerCase();
    return hay.includes(q);
  });
}

// Real chz labels look like (see characterize_main.cpp:59, CharacterizationRunner.cpp:591+):
//   "[moog] moog/LP12/fc50 B1 res0.00 drv0.00 os2 render 88200"   (full B1/B3 shape)
//   "[moog] moog/LP24/fc250 B2 selfosc"                            (short B2 shape — no res/drv/os/rate)
// The leading "[model] " prefix is optional (added by characterize_main's progress callback);
// fields after the battery token are optional and simply omitted from the rendered prose
// when absent — never rendered as the literal string "undefined".
const LABEL_RE =
  /^(?:\[[^\]]+\]\s+)?(?<model>[^/\s]+)\/(?<mode>[^/\s]+)\/fc(?<fc>[0-9.]+)\s+(?<battery>B[1-4])(?:\s+res(?<res>[0-9.]+))?(?:\s+drv(?<drv>[0-9.]+))?(?:\s+os(?<os>[0-9]+))?(?:\s+(?<liveRender>live|render))?(?:\s+(?<rate>[0-9]+))?/;

interface ParsedLabel {
  model: string;
  mode: string;
  fc: string;
  battery: string;
  res?: string;
  drv?: string;
  os?: string;
  liveRender?: string;
  rate?: string;
}

function parseLabel(label: string): ParsedLabel | null {
  const m = LABEL_RE.exec(label);
  if (!m || !m.groups) return null;
  const { model, mode, fc, battery, res, drv, os, liveRender, rate } = m.groups;
  if (!model || !mode || !fc || !battery) return null;
  return { model, mode, fc, battery, res, drv, os, liveRender, rate };
}

/** Renders the optional B1 detail clause, omitting any field that wasn't present (never "undefined"). */
function b1DetailClause(p: ParsedLabel): string {
  const parts: string[] = [];
  if (p.res !== undefined) parts.push(`res ${p.res}`);
  if (p.drv !== undefined) parts.push(`drive ${p.drv}`);
  if (p.os !== undefined) parts.push(`${p.os}x oversampling`);
  if (p.rate !== undefined) parts.push(`${p.rate} Hz`);
  if (p.liveRender !== undefined) parts.push(p.liveRender);
  return parts.join(", ");
}

function renderBattery(p: ParsedLabel): { title: string; body: string } {
  switch (p.battery) {
    case "B1":
      return {
        title: "Magnitude response (dual-method)",
        body: `Measures the frequency response of ${p.model} ${p.mode} at cutoff ${p.fc} Hz (${b1DetailClause(p)}). Two independent methods — stepped-sine and ESS deconvolution — must agree within 1 dB; disagreement means the measurement itself cannot be trusted (see docs/filter-validation/acceptance-criterion.md).`,
      };
    case "B2":
      return {
        title: "Resonance & self-oscillation",
        body: `At maximum resonance, measures the self-oscillation pitch against the commanded cutoff (±3% gate below ~4 kHz per the ratified standard) and the resonant peak behavior.`,
      };
    case "B3":
      return {
        title: "THD & aliasing split",
        body: `Drives the filter into its nonlinearity and splits distortion into harmonic content vs aliased content per oversampling tier — aliasing must fall as the OS factor rises.`,
      };
    case "B4":
      return {
        title: "Phase / group delay (descriptive only)",
        body: `Records phase and group delay from the deconvolved impulse response. Descriptive-only per register Q20 (IR not time-aligned); numbers are reported, not gated.`,
      };
    default:
      return { title: p.battery, body: "Unrecognized operating-point label." };
  }
}

const FALLBACK_BODY = "Unrecognized operating-point label.";

/** Parses a chz progress-line label into a human-readable {title, body}. Unknown shapes fall back verbatim. */
export function explainChzLabel(label: string): { title: string; body: string } {
  const parsed = parseLabel(label);
  if (!parsed) return { title: label, body: FALLBACK_BODY };
  return renderBattery(parsed);
}
