import { test } from "node:test";
import assert from "node:assert/strict";
import {
  renderActive,
  renderCiStrip,
  renderForm,
  renderArchive,
  renderRunDetail,
  deviationRows,
  esc,
  humanizeMs,
} from "./franklinRender.ts";
import type { RunSummary, RunDetail } from "./franklinTypes.ts";
import type { CiPayload } from "../server/ci.ts";
import type { StaleInfo, Template } from "../server/control.ts";
import type { CatalogEntry } from "./franklinExplain.ts";

// ---- fixtures --------------------------------------------------------------

const EMPTY_CI: CiPayload = { available: false, fetchedAt: 0, branches: [] };
const NOW = 1_050_000;

// The fixed template whitelist, mirroring server/control.ts's templates().
// renderForm now takes the real Template[] the server already ships alongside
// stale info (server.ts returns { templates: templates(), stale }).
function templates(): Template[] {
  return [
    { id: "suite", label: "Full suite", bin: "build/tests/k2000_tests", args: [], env: {} },
    { id: "suite-disparity", label: "Suite (disparity sweep)", bin: "build/tests/k2000_tests", args: [], env: {} },
    { id: "suite-voiceperf", label: "Suite (voice perf)", bin: "build/tests/k2000_tests", args: [], env: {} },
    { id: "chz", label: "Device characterization", bin: "build/tests/k2000_device_characterization", args: [], env: {} },
  ];
}

// The re-run payload ships inside a data-rerun HTML attribute (quotes escaped
// to &quot;). The browser un-escapes it to el.dataset.rerun before JSON.parse;
// this helper reproduces that decode so we can assert the recovered object —
// the actual contract — rather than escaping incidentals.
function firstRerunPayload(html: string): { templateId: string; params: Record<string, string> } {
  const m = /data-rerun='([^']*)'/.exec(html);
  assert.ok(m, "an archive row exposes a data-rerun attribute");
  const decoded = m![1]
    .replace(/&#39;/g, "'").replace(/&quot;/g, '"')
    .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&amp;/g, "&");
  return JSON.parse(decoded);
}

function runningChz(over: Partial<RunSummary> = {}): RunSummary {
  return {
    id: "20260703-160000-chz.ndjson",
    kind: "chz",
    startedAt: 1_000_000,
    status: "running",
    sizeBytes: 2048,
    done: 20,
    total: 100,
    label: "[moog] moog/LP12/fc50 B1 res0.00 drv0.00 os2 render 88200",
    model: "moog",
    grid: "quick",
    pid: 4242,
    ...over,
  };
}

function detail(over: Partial<RunDetail> = {}): RunDetail {
  return {
    id: "20260703-160000-chz.ndjson",
    kind: "chz",
    startedAt: 1_000_000,
    status: "fail",
    sizeBytes: 4096,
    model: "moog",
    grid: "quick",
    argv: ["./build/tests/k2000_device_characterization", "--model", "moog", "--quick"],
    gitSha: "abcdef1234567890",
    buildType: "Release",
    testsList: [],
    checks: [],
    progressTail: [],
    ...over,
  };
}

// ---- esc -------------------------------------------------------------------

test("esc neutralizes angle brackets, ampersands, and quotes", () => {
  assert.equal(esc(`a<b>&"c`), `a&lt;b&gt;&amp;&quot;c`);
});

// ---- humanizeMs ------------------------------------------------------------

test("humanizeMs: seconds under a minute", () => {
  assert.match(humanizeMs(45_000), /45\s*s/);
});

test("humanizeMs: minutes", () => {
  assert.equal(humanizeMs(150_000), "2.5 m"); // 150s = 2.5 min
});

test("humanizeMs: hours", () => {
  assert.match(humanizeMs(3 * 3_600_000 + 60_000), /3\s*h/);
});

// ---- renderActive: running chz card ----------------------------------------

test("renderActive: running chz card shows done/total, percent, and a live status", () => {
  const html = renderActive([runningChz()], NOW);
  assert.match(html, /20\s*\/\s*100/); // done/total
  assert.match(html, /20\s*%/); // percent (20/100)
  assert.match(html, /status-running/);
  assert.match(html, /data-action="stop"/); // running cards are stoppable
});

test("renderActive: running chz card computes ETA from now, startedAt, done, total", () => {
  // elapsed = now - startedAt = 100s over 20 done => 5s/item; 80 remain => 400s ETA => ~6-7 min
  const now = 1_000_000 + 100_000;
  const html = renderActive([runningChz()], now);
  assert.match(html, /ETA/);
  assert.match(html, /6\s*m|7\s*m/);
});

test("renderActive: running chz card surfaces the explained label title, not the raw label", () => {
  const html = renderActive([runningChz()], NOW);
  assert.match(html, /Magnitude response/); // explainChzLabel B1 title
});

test("renderActive: running chz card with done=0 does not divide-by-zero or print NaN/Infinity", () => {
  const html = renderActive([runningChz({ done: 0 })], NOW);
  assert.doesNotMatch(html, /NaN|Infinity/);
});

test("renderActive: stalled run shows a stalled badge and remains stoppable", () => {
  const html = renderActive([runningChz({ status: "stalled" })], NOW);
  assert.match(html, /status-stalled/);
  assert.match(html, /stalled/i);
  assert.match(html, /data-action="stop"/);
});

test("renderActive: shows an empty note and no cards when nothing is running", () => {
  const html = renderActive([], NOW);
  assert.match(html, /No active runs/i);
  assert.doesNotMatch(html, /data-run-id=/);
});

test("renderActive: renders ONLY active runs, never finished ones", () => {
  const finished: RunSummary = {
    id: "done.ndjson", kind: "suite", startedAt: 1, status: "pass", sizeBytes: 1, durationS: 5,
  };
  const html = renderActive([runningChz(), finished], NOW);
  assert.match(html, /20260703-160000-chz/); // the running one is present
  assert.doesNotMatch(html, /done\.ndjson/); // finished ones are the archive's job
  // no archive scaffolding leaks into the active section
  assert.doesNotMatch(html, /fr-arch-table/);
});

// ---- renderCiStrip ---------------------------------------------------------

test("renderCiStrip: renders each branch and check when available", () => {
  const ci: CiPayload = {
    available: true, fetchedAt: 0,
    branches: [
      { ref: "feat/x", title: "My PR", checks: [{ name: "build", status: "completed", conclusion: "success", url: "http://ci/1" }] },
    ],
  };
  const html = renderCiStrip(ci);
  assert.match(html, /My PR/);
  assert.match(html, /build/);
});

test("renderCiStrip: shows an unavailable note when gh is down (never crashes)", () => {
  const html = renderCiStrip(EMPTY_CI);
  assert.match(html, /CI/); // section still present
});

test("renderCiStrip: never renders a non-http(s) check url as a clickable href", () => {
  const ci: CiPayload = {
    available: true, fetchedAt: 0,
    branches: [
      { ref: "feat/x", title: "My PR", checks: [{ name: "build", status: "completed", conclusion: "success", url: "javascript:alert(1)" }] },
    ],
  };
  const html = renderCiStrip(ci);
  assert.match(html, /build/); // check name still shown as plain text
  assert.doesNotMatch(html, /href="javascript:/);
});

test("renderCiStrip: still renders a normal https check url as a clickable anchor", () => {
  const ci: CiPayload = {
    available: true, fetchedAt: 0,
    branches: [
      { ref: "feat/x", title: "My PR", checks: [{ name: "build", status: "completed", conclusion: "success", url: "https://github.com/example/repo/actions/runs/1" }] },
    ],
  };
  const html = renderCiStrip(ci);
  assert.match(html, /href="https:\/\/github\.com\/example\/repo\/actions\/runs\/1"/);
});

// ---- renderForm ------------------------------------------------------------

test("renderForm: exposes the template <select> and the chz model/grid selects", () => {
  const html = renderForm(templates(), []);
  assert.match(html, /data-role="template"/);
  assert.match(html, /data-role="model"/);
  assert.match(html, /data-role="grid"/);
  assert.match(html, /<select/); // the native control the dropdown fix protects
  assert.match(html, /Full suite/); // a template label from the passed Template[]
  assert.match(html, /Device characterization/);
});

test("renderForm: marks the chz template option so the client can show model/grid", () => {
  const html = renderForm(templates(), []);
  // the chz option carries data-chz="1"; suite options carry data-chz="0"
  assert.match(html, /data-chz="1"/);
  assert.match(html, /data-chz="0"/);
});

test("renderForm: shows a stale-binary warning chip matched by binary PATH not index", () => {
  const stale: StaleInfo[] = [
    { binary: "build/tests/k2000_device_characterization", stale: true, binaryMtimeMs: 1, newestSourceMtimeMs: 2 },
    { binary: "build/tests/k2000_tests", stale: false, binaryMtimeMs: 5, newestSourceMtimeMs: 2 },
  ];
  const html = renderForm(templates(), stale);
  assert.match(html, /stale/i);
});

test("renderForm: no stale warning surfaces when every binary is fresh", () => {
  const stale: StaleInfo[] = [
    { binary: "build/tests/k2000_device_characterization", stale: false, binaryMtimeMs: 5, newestSourceMtimeMs: 2 },
    { binary: "build/tests/k2000_tests", stale: false, binaryMtimeMs: 5, newestSourceMtimeMs: 2 },
  ];
  const html = renderForm(templates(), stale);
  // the always-visible summary chip is present but hidden and empty
  assert.match(html, /data-role="stale-chip"/);
  assert.doesNotMatch(html, /may be stale/);
});

test("renderForm: is byte-for-byte deterministic for identical inputs (no timestamps/randomness)", () => {
  const t = templates();
  const s: StaleInfo[] = [{ binary: "build/tests/k2000_tests", stale: true, binaryMtimeMs: 1, newestSourceMtimeMs: 2 }];
  // This is the property the dropdown fix leans on: a steady-state re-render
  // produces identical HTML, so the section poller can skip touching the DOM.
  assert.equal(renderForm(t, s), renderForm(t, s));
});

test("renderForm: contains the form but NONE of the archive's rows/table", () => {
  const html = renderForm(templates(), []);
  assert.match(html, /data-role="newrun-form"/);
  assert.doesNotMatch(html, /fr-arch-table/);
  assert.doesNotMatch(html, /data-action="rerun"/);
});

// ---- renderArchive ---------------------------------------------------------

test("renderArchive: finished suite run appears with a humanized duration and outcome", () => {
  const done: RunSummary = {
    id: "20260703-150000-suite.ndjson", kind: "suite", startedAt: 1_000_000,
    status: "pass", sizeBytes: 48174, durationS: 16.6, tests: 291, failed: 0,
  };
  const html = renderArchive([done], 48174);
  assert.match(html, /status-pass/);
  assert.match(html, /16\.6\s*s/); // humanized duration from durationS (one decimal)
  assert.match(html, /pass/);
});

test("renderArchive: suite row offers a plain re-run carrying templateId suite", () => {
  const done: RunSummary = {
    id: "s.ndjson", kind: "suite", startedAt: 1, status: "pass", sizeBytes: 1, durationS: 5,
  };
  const html = renderArchive([done], 1);
  assert.match(html, /data-action="rerun"/);
  assert.match(html, /re-run \(plain\)/); // suite env flags not recoverable
  assert.equal(firstRerunPayload(html).templateId, "suite");
});

test("renderArchive: chz row re-run carries templateId chz plus model/grid params", () => {
  const done: RunSummary = {
    id: "c.ndjson", kind: "chz", startedAt: 1, status: "fail", sizeBytes: 1,
    durationS: 5, model: "huggett", grid: "full",
  };
  const html = renderArchive([done], 1);
  assert.match(html, /data-action="rerun"/);
  const payload = firstRerunPayload(html);
  assert.equal(payload.templateId, "chz");
  assert.equal(payload.params.model, "huggett");
  assert.equal(payload.params.grid, "full");
});

test("renderArchive: disk footprint renders in MB with one decimal in the header", () => {
  const html = renderArchive([], 3_500_000);
  assert.match(html, /3\.3\s*MB|3\.5\s*MB/); // 3_500_000 bytes ~ 3.3 MiB or 3.5 MB
});

test("renderArchive: carries the detail drawer mount point", () => {
  const html = renderArchive([], 0);
  assert.match(html, /data-role="detail-drawer"/);
});

test("renderArchive: shows an empty note and no rows when there is no history", () => {
  const html = renderArchive([], 0);
  assert.match(html, /No archived runs/i);
  assert.doesNotMatch(html, /data-action="rerun"/);
});

test("renderArchive: renders ONLY finished runs, never active ones", () => {
  const finished: RunSummary = {
    id: "done.ndjson", kind: "suite", startedAt: 1, status: "pass", sizeBytes: 1, durationS: 5,
  };
  const html = renderArchive([runningChz(), finished], 0);
  assert.match(html, /done\.ndjson/);
  assert.doesNotMatch(html, /20260703-160000-chz/); // the running one belongs to renderActive
});

test("renderArchive: contains rows but NONE of the new-run form", () => {
  const done: RunSummary = {
    id: "s.ndjson", kind: "suite", startedAt: 1, status: "pass", sizeBytes: 1, durationS: 5,
  };
  const html = renderArchive([done], 1);
  assert.match(html, /fr-arch-table/);
  assert.doesNotMatch(html, /data-role="newrun-form"/);
  assert.doesNotMatch(html, /data-role="template"/);
});

// ---- deviationRows ---------------------------------------------------------

test("deviationRows: a failed-test message is severity 0, one row per message", () => {
  const d = detail({
    status: "fail",
    testsList: [
      { name: "Filter", sub: "LP24 magnitude", ok: false, passes: 3, failures: 2, messages: ["expected -3dB got +45dB", "peak out of band"] },
    ],
  });
  const rows = deviationRows(d);
  const sev0 = rows.filter((r) => r.severity === 0);
  assert.equal(sev0.length, 2); // one per failure message
  assert.match(sev0[0].detail + sev0[1].detail, /45dB/);
});

test("deviationRows: a check with verdict fail is severity 0", () => {
  const d = detail({
    checks: [{ name: "peak_db", measured: 88.9, expected: 4.0, verdict: "fail" }],
  });
  const rows = deviationRows(d);
  assert.ok(rows.some((r) => r.severity === 0 && /peak_db/.test(r.title)));
});

test("deviationRows: run-state anomaly (stalled) is severity 1", () => {
  const d = detail({ status: "stalled", testsList: [], checks: [] });
  const rows = deviationRows(d);
  assert.ok(rows.some((r) => r.severity === 1 && /stall/i.test(r.title + r.detail)));
});

test("deviationRows: error and stopped states also yield a severity-1 row", () => {
  assert.ok(deviationRows(detail({ status: "error" })).some((r) => r.severity === 1));
  assert.ok(deviationRows(detail({ status: "stopped" })).some((r) => r.severity === 1));
  // pass/running produce no state anomaly row
  assert.equal(deviationRows(detail({ status: "pass" })).filter((r) => r.severity === 1).length, 0);
});

test("deviationRows: a passing check WITH expected is a severity-2 margin row with measured/expected/delta", () => {
  const d = detail({
    status: "pass",
    checks: [{ name: "cutoff_hz", measured: 1010, expected: 1000, verdict: "pass" }],
  });
  const rows = deviationRows(d);
  const margin = rows.find((r) => r.severity === 2 && /cutoff_hz/.test(r.title));
  assert.ok(margin);
  assert.match(margin!.detail, /1010/);
  assert.match(margin!.detail, /1000/);
  assert.match(margin!.detail, /10/); // delta
});

test("deviationRows: named always-show margins appear even when passing WITHOUT expected", () => {
  const d = detail({
    status: "pass",
    checks: [
      { name: "method_delta_db", measured: 0.4, verdict: "pass" },
      { name: "selfosc_cents_err", measured: 12, verdict: "pass" },
      { name: "noise_floor_dbfs", measured: -96, verdict: "info" },
      { name: "unremarkable", measured: 1, verdict: "pass" }, // NOT shown
    ],
  });
  const rows = deviationRows(d);
  assert.ok(rows.some((r) => r.severity === 2 && /method_delta_db/.test(r.title)));
  assert.ok(rows.some((r) => r.severity === 2 && /selfosc_cents_err/.test(r.title)));
  assert.ok(rows.some((r) => r.severity === 2 && /noise_floor_dbfs/.test(r.title)));
  assert.ok(!rows.some((r) => /unremarkable/.test(r.title)));
});

test("deviationRows: sorted severity ascending, then |measured-expected| descending; no-expected rows last within severity", () => {
  const d = detail({
    status: "stalled", // severity 1
    testsList: [
      { name: "T", sub: "s", ok: false, passes: 0, failures: 1, messages: ["boom"] }, // severity 0
    ],
    checks: [
      { name: "small_margin", measured: 101, expected: 100, verdict: "pass" }, // sev2 delta 1
      { name: "big_margin", measured: 200, expected: 100, verdict: "pass" }, // sev2 delta 100
      { name: "method_delta_db", measured: 0.4, verdict: "pass" }, // sev2 no expected -> last
    ],
  });
  const rows = deviationRows(d);
  // severity order
  const severities = rows.map((r) => r.severity);
  const sorted = [...severities].sort((a, b) => a - b);
  assert.deepEqual(severities, sorted);
  // within severity 2: big_margin (delta 100) before small_margin (delta 1) before method_delta_db (no expected)
  const sev2 = rows.filter((r) => r.severity === 2);
  const idxBig = sev2.findIndex((r) => /big_margin/.test(r.title));
  const idxSmall = sev2.findIndex((r) => /small_margin/.test(r.title));
  const idxNoExp = sev2.findIndex((r) => /method_delta_db/.test(r.title));
  assert.ok(idxBig < idxSmall, "bigger delta first");
  assert.ok(idxSmall < idxNoExp, "no-expected row last");
});

// ---- renderRunDetail -------------------------------------------------------

function catalog(): CatalogEntry[] {
  return [
    {
      key: "Filter / LP24 magnitude",
      file: "tests/FilterTests.cpp",
      what: "Sweeps LP24 magnitude response.",
      why: "Pins the filter shape.",
      deviationMeans: "The filter shape drifted.",
      links: ["docs/filter-validation/acceptance-criterion.md"],
    },
  ];
}

test("renderRunDetail: deviations panel is rendered before the tests list", () => {
  const d = detail({
    status: "fail",
    testsList: [{ name: "Filter", sub: "LP24 magnitude", ok: false, passes: 0, failures: 1, messages: ["bad"] }],
  });
  const html = renderRunDetail(d, catalog());
  const devIdx = html.indexOf("Deviations");
  const testsIdx = html.search(/Tests|Test results/);
  assert.ok(devIdx >= 0, "has a Deviations section");
  assert.ok(testsIdx >= 0, "has a Tests section");
  assert.ok(devIdx < testsIdx, "deviations come first");
});

test("renderRunDetail: a failing test shows its messages in a red-highlighted block and catalog prose", () => {
  const d = detail({
    status: "fail",
    testsList: [{ name: "Filter", sub: "LP24 magnitude", ok: false, passes: 0, failures: 1, messages: ["expected -3dB got +45dB"] }],
  });
  const html = renderRunDetail(d, catalog());
  assert.match(html, /expected -3dB got \+45dB/);
  assert.match(html, /msg-fail|deviation-fail|fail-message/); // red-highlight class
  assert.match(html, /Sweeps LP24 magnitude response/); // catalog "what"
  assert.match(html, /The filter shape drifted/); // catalog deviationMeans
});

test("renderRunDetail: a passing test does NOT dump its messages red (catalog prose still available)", () => {
  const d = detail({
    status: "pass",
    testsList: [{ name: "Filter", sub: "LP24 magnitude", ok: true, passes: 5, failures: 0, messages: [] }],
  });
  const html = renderRunDetail(d, catalog());
  assert.doesNotMatch(html, /deviation-fail/);
});

test("renderRunDetail: chz label explanation (B1) appears for a chz run's last progress label", () => {
  const d = detail({
    kind: "chz",
    progressTail: [{ ts: 1, done: 99, total: 100, label: "[moog] moog/LP12/fc50 B1 res0.00 drv0.00 os2 render 88200" }],
  });
  const html = renderRunDetail(d, catalog());
  assert.match(html, /Magnitude response \(dual-method\)/); // explainChzLabel B1 title
});

test("renderRunDetail: shows argv, gitSha, buildType metadata and the CSV path for chz", () => {
  const d = detail({ kind: "chz", model: "moog" });
  const html = renderRunDetail(d, catalog());
  assert.match(html, /Release/); // buildType
  assert.match(html, /abcdef1/); // gitSha (may be shortened)
  assert.match(html, /--model/); // argv
  assert.match(html, /build\/characterization\/moog\//); // CSV path note
});

test("renderRunDetail: escapes hostile message text (no raw script tag)", () => {
  const d = detail({
    status: "fail",
    testsList: [{ name: "X", sub: "y", ok: false, passes: 0, failures: 1, messages: ["<script>alert(1)</script>"] }],
  });
  const html = renderRunDetail(d, catalog());
  assert.doesNotMatch(html, /<script>alert/);
  assert.match(html, /&lt;script&gt;/);
});
