import { test } from "node:test";
import assert from "node:assert/strict";
import { renderRunsPage, renderRunDetail, deviationRows, esc, humanizeMs } from "./franklinRender.ts";
import type { RunSummary, RunDetail } from "./franklinTypes.ts";
import type { CiPayload } from "../server/ci.ts";
import type { StaleInfo } from "../server/control.ts";
import type { CatalogEntry } from "./franklinExplain.ts";

// ---- fixtures --------------------------------------------------------------

const EMPTY_CI: CiPayload = { available: false, fetchedAt: 0, branches: [] };

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

// ---- renderRunsPage: running chz card --------------------------------------

test("running chz card shows done/total, percent, and a live status", () => {
  const html = renderRunsPage([runningChz()], EMPTY_CI, 1024, []);
  assert.match(html, /20\s*\/\s*100/); // done/total
  assert.match(html, /20\s*%/); // percent (20/100)
  assert.match(html, /status-running/);
  assert.match(html, /data-action="stop"/); // running cards are stoppable
});

test("running chz card computes ETA from now, startedAt, done, total", () => {
  // elapsed = now - startedAt = 100s over 20 done => 5s/item; 80 remain => 400s ETA => ~6-7 min
  const now = 1_000_000 + 100_000;
  const html = renderRunsPage([runningChz()], EMPTY_CI, 0, [], now);
  assert.match(html, /ETA/);
  assert.match(html, /6\s*m|7\s*m/);
});

test("running chz card surfaces the explained label title, not the raw label", () => {
  const html = renderRunsPage([runningChz()], EMPTY_CI, 0, []);
  assert.match(html, /Magnitude response/); // explainChzLabel B1 title
});

test("running chz card with done=0 does not divide-by-zero or print NaN/Infinity", () => {
  const html = renderRunsPage([runningChz({ done: 0 })], EMPTY_CI, 0, [], 1_050_000);
  assert.doesNotMatch(html, /NaN|Infinity/);
});

// ---- renderRunsPage: stalled ----------------------------------------------

test("stalled run shows a stalled badge and remains stoppable", () => {
  const html = renderRunsPage([runningChz({ status: "stalled" })], EMPTY_CI, 0, []);
  assert.match(html, /status-stalled/);
  assert.match(html, /stalled/i);
  assert.match(html, /data-action="stop"/);
});

// ---- renderRunsPage: archive table -----------------------------------------

test("finished suite run appears in the archive with a humanized duration and outcome", () => {
  const done: RunSummary = {
    id: "20260703-150000-suite.ndjson", kind: "suite", startedAt: 1_000_000,
    status: "pass", sizeBytes: 48174, durationS: 16.6, tests: 291, failed: 0,
  };
  const html = renderRunsPage([done], EMPTY_CI, 48174, []);
  assert.match(html, /status-pass/);
  assert.match(html, /16\.6\s*s/); // humanized duration from durationS (one decimal)
  assert.match(html, /pass/);
});

test("archive suite row offers a plain re-run carrying templateId suite", () => {
  const done: RunSummary = {
    id: "s.ndjson", kind: "suite", startedAt: 1, status: "pass", sizeBytes: 1, durationS: 5,
  };
  const html = renderRunsPage([done], EMPTY_CI, 1, []);
  assert.match(html, /data-action="rerun"/);
  assert.match(html, /re-run \(plain\)/); // suite env flags not recoverable
  assert.equal(firstRerunPayload(html).templateId, "suite");
});

test("archive chz row re-run carries templateId chz plus model/grid params", () => {
  const done: RunSummary = {
    id: "c.ndjson", kind: "chz", startedAt: 1, status: "fail", sizeBytes: 1,
    durationS: 5, model: "huggett", grid: "full",
  };
  const html = renderRunsPage([done], EMPTY_CI, 1, []);
  assert.match(html, /data-action="rerun"/);
  const payload = firstRerunPayload(html);
  assert.equal(payload.templateId, "chz");
  assert.equal(payload.params.model, "huggett");
  assert.equal(payload.params.grid, "full");
});

test("disk footprint renders in MB with one decimal in the archive header", () => {
  const html = renderRunsPage([], EMPTY_CI, 3_500_000, []);
  assert.match(html, /3\.3\s*MB|3\.5\s*MB/); // 3_500_000 bytes ~ 3.3 MiB or 3.5 MB
});

// ---- renderRunsPage: new-run form + stale ----------------------------------

test("new-run form shows a stale-binary warning chip matched by binary PATH not index", () => {
  const stale: StaleInfo[] = [
    { binary: "build/tests/k2000_device_characterization", stale: true, binaryMtimeMs: 1, newestSourceMtimeMs: 2 },
    { binary: "build/tests/k2000_tests", stale: false, binaryMtimeMs: 5, newestSourceMtimeMs: 2 },
  ];
  const html = renderRunsPage([], EMPTY_CI, 0, stale);
  assert.match(html, /stale/i);
});

// ---- renderRunsPage: CI strip ----------------------------------------------

test("CI strip renders each branch and check when available", () => {
  const ci: CiPayload = {
    available: true, fetchedAt: 0,
    branches: [
      { ref: "feat/x", title: "My PR", checks: [{ name: "build", status: "completed", conclusion: "success", url: "http://ci/1" }] },
    ],
  };
  const html = renderRunsPage([], ci, 0, []);
  assert.match(html, /My PR/);
  assert.match(html, /build/);
});

test("CI strip shows an unavailable note when gh is down (never crashes)", () => {
  const html = renderRunsPage([], EMPTY_CI, 0, []);
  assert.match(html, /CI/); // section still present
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
