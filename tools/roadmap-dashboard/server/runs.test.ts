import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, writeFile, readFile, stat } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import {
  parseRun, listRuns, readRun, compactFinished,
  STALL_MS, COMPACT_MARKER, MAX_PROGRESS_KEPT,
} from "./runs.ts";
import { runnerLabel } from "../src/franklinTypes.ts";

async function tmpDir(): Promise<string> {
  return mkdtemp(join(tmpdir(), "franklin-runs-"));
}

// Real event shapes lifted from actual .franklin/runs/*.ndjson output (Task 1-3).
const SUITE_START = '{"ev":"start","ts":1783116948810,"kind":"suite","argv":["./build/tests/k2000_tests"],"pid":342689,"buildType":"Release","gitSha":"de6bff5"}';
const CHZ_START = '{"ev":"start","ts":1783116775571,"kind":"chz","argv":["./build/tests/k2000_device_characterization","--model","moog","--quick"],"pid":341448,"buildType":"Release","model":"moog","grid":"quick"}';
function progressLine(done: number, total: number, label: string, ts = 1783116775572 + done): string {
  return JSON.stringify({ ev: "progress", ts, done, total, label });
}
function testLine(name: string, sub: string, ok = true): string {
  return JSON.stringify({ ev: "test", ts: 1783116965550, name, sub, ok, passes: ok ? 1 : 0, failures: ok ? 0 : 1, messages: [] });
}
const SUITE_END_PASS = '{"ev":"end","ts":1783116965554,"outcome":"pass","durationS":16.744,"tests":291,"failed":0,"checks":[]}';

test("parseRun: complete suite run parses to status pass w/ tests/failed populated", () => {
  const text = [
    SUITE_START,
    testLine("Smoke", "test harness is wired"),
    testLine("ParamSnapshot", "defaults match expected values"),
    SUITE_END_PASS,
  ].join("\n") + "\n";
  const nowMs = 1783116970000;
  const { summary, detail } = parseRun("20260703-161548-suite-342689.ndjson", text, nowMs, nowMs, text.length);

  assert.equal(summary.kind, "suite");
  assert.equal(summary.status, "pass");
  assert.equal(summary.startedAt, 1783116948810);
  assert.equal(summary.tests, 291);
  assert.equal(summary.failed, 0);
  assert.equal(summary.durationS, 16.744);
  assert.equal(summary.pid, 342689);
  assert.equal(summary.sizeBytes, text.length);
  assert.equal(detail.testsList.length, 2);
  assert.equal(detail.testsList[0].name, "Smoke");
  assert.equal(detail.buildType, "Release");
  assert.equal(detail.gitSha, "de6bff5");
  assert.deepEqual(detail.checks, []);
});

test("parseRun: chz run w/o end + fresh mtime is running, done/total/label from last progress", () => {
  const text = [
    CHZ_START,
    progressLine(1, 126, "[moog] moog/LP24/fc250 B2 selfosc"),
    progressLine(3, 126, "[moog] moog/LP24/fc250 B1 res0.00 drv0.00 os1 live 96000"),
    progressLine(49, 126, "[moog] moog/BP/fc250 B1 res0.90 drv0.00 os1 live 96000"),
  ].join("\n") + "\n";
  const mtimeMs = 1783116864100;
  const nowMs = mtimeMs + 5_000; // well within STALL_MS
  const { summary, detail } = parseRun("20260703-161255-chz-341448.ndjson", text, mtimeMs, nowMs, text.length);

  assert.equal(summary.kind, "chz");
  assert.equal(summary.status, "running");
  assert.equal(summary.model, "moog");
  assert.equal(summary.grid, "quick");
  assert.equal(summary.done, 49);
  assert.equal(summary.total, 126);
  assert.equal(summary.label, "[moog] moog/BP/fc250 B1 res0.90 drv0.00 os1 live 96000");
  assert.equal(detail.progressTail.at(-1)?.done, 49);
});

test("parseRun: no end + stale mtime beyond STALL_MS is stalled", () => {
  const text = [CHZ_START, progressLine(1, 126, "only one progress line")].join("\n") + "\n";
  const mtimeMs = 1_000_000;
  const nowMs = mtimeMs + STALL_MS + 1;
  const { summary } = parseRun("id.ndjson", text, mtimeMs, nowMs, text.length);
  assert.equal(summary.status, "stalled");
});

test("parseRun: truncated final line is skipped, not thrown", () => {
  const good = [SUITE_START, testLine("Smoke", "wired")].join("\n") + "\n";
  const truncated = good + '{"ev":"test","ts":1783116965550,"name":"Cut'; // no closing brace, no newline
  const nowMs = 1783116970000;
  assert.doesNotThrow(() => parseRun("id.ndjson", truncated, nowMs, nowMs, truncated.length));
  const { summary, detail } = parseRun("id.ndjson", truncated, nowMs, nowMs, truncated.length);
  assert.equal(detail.testsList.length, 1); // only the well-formed test line counted
  assert.equal(summary.status, "running"); // no end event parsed, mtime fresh
});

test("listRuns: orders newest first and sets sizeBytes from the real file", async () => {
  const dir = await tmpDir();
  const older = JSON.stringify({ ev: "start", ts: 1000, kind: "suite", argv: [], pid: 1, buildType: "Release" }) + "\n" + SUITE_END_PASS + "\n";
  const newer = JSON.stringify({ ev: "start", ts: 2000, kind: "suite", argv: [], pid: 2, buildType: "Release" }) + "\n" + SUITE_END_PASS + "\n";
  await writeFile(join(dir, "a-older.ndjson"), older);
  await writeFile(join(dir, "b-newer.ndjson"), newer);

  const runs = await listRuns(dir);

  assert.equal(runs.length, 2);
  assert.equal(runs[0].startedAt, 2000);
  assert.equal(runs[1].startedAt, 1000);
  const st = await stat(join(dir, "b-newer.ndjson"));
  assert.equal(runs[0].sizeBytes, st.size);
});

test("compactFinished: shrinks a 2000-progress-line finished file to <=500+marker+start+end, and is idempotent", async () => {
  const dir = await tmpDir();
  const lines = [CHZ_START];
  for (let i = 1; i <= 2000; i++) lines.push(progressLine(i, 2000, `step ${i}`, 1783116775571 + i));
  lines.push(testLine("Check", "sub"));
  const endLine = '{"ev":"end","ts":1783116965554,"outcome":"pass","durationS":1.0,"tests":1,"failed":0,"checks":[]}';
  lines.push(endLine);
  const file = join(dir, "big.ndjson");
  await writeFile(file, lines.join("\n") + "\n");

  const compactedCount = await compactFinished(dir);
  assert.equal(compactedCount, 1);

  const rewritten = (await readFile(file, "utf8")).split("\n").filter((l) => l.length > 0);
  assert.equal(rewritten[0], COMPACT_MARKER);
  const progressLines = rewritten.filter((l) => JSON.parse(l).ev === "progress");
  const progressCount = progressLines.length;
  assert.ok(progressCount <= MAX_PROGRESS_KEPT, `expected <=${MAX_PROGRESS_KEPT} progress lines, got ${progressCount}`);
  assert.ok(rewritten.some((l) => JSON.parse(l).ev === "start"));
  assert.ok(rewritten.some((l) => JSON.parse(l).ev === "test"));
  const last = JSON.parse(rewritten[rewritten.length - 1]);
  assert.equal(last.ev, "end");

  // Verify the final progress event is preserved with the correct done value
  const finalProgress = JSON.parse(progressLines[progressLines.length - 1]);
  assert.equal(finalProgress.done, 2000, "final progress event should have done=2000");
  assert.equal(finalProgress.total, 2000, "final progress event should have total=2000");
  assert.equal(finalProgress.label, "step 2000", "final progress event should have label='step 2000'");

  // idempotent: second pass finds nothing left to compact (marker already present)
  const again = await compactFinished(dir);
  assert.equal(again, 0);
  const stillThere = await readFile(file, "utf8");
  assert.equal(stillThere, rewritten.join("\n") + "\n");
});

test("readRun rejects path-traversal ids", async () => {
  const dir = await tmpDir();
  assert.equal(await readRun(dir, "../etc/passwd"), null);
  assert.equal(await readRun(dir, "..\\etc\\passwd"), null);
  assert.equal(await readRun(dir, "sub/dir.ndjson"), null);
  assert.equal(await readRun(dir, "nonexistent.ndjson"), null); // valid id, missing file -> null too
});

// --- Task 2: runner provenance + unknown-kind tolerance ---------------------

test("parseRun: start event with runner copies it into summary and detail", () => {
  const text = [
    '{"ev":"start","ts":1783116948810,"kind":"suite","argv":["./build/tests/k2000_tests"],"pid":342689,"buildType":"Release","runner":"terminal"}',
    SUITE_END_PASS,
  ].join("\n") + "\n";
  const nowMs = 1783116970000;
  const { summary, detail } = parseRun("id.ndjson", text, nowMs, nowMs, text.length);

  assert.equal(summary.runner, "terminal");
  assert.equal(detail.runner, "terminal");
});

test("parseRun: start event without runner leaves summary.runner undefined", () => {
  const text = [SUITE_START, SUITE_END_PASS].join("\n") + "\n"; // SUITE_START has no runner field
  const nowMs = 1783116970000;
  const { summary, detail } = parseRun("id.ndjson", text, nowMs, nowMs, text.length);

  assert.equal(summary.runner, undefined);
  assert.equal(detail.runner, undefined);
});

test("parseRun: unrecognized kind (capture) flows through with status logic intact", () => {
  const text = [
    '{"ev":"start","ts":1783116948810,"kind":"capture","argv":["./build/tests/k2000_capture_tool"],"pid":9001,"buildType":"Release","runner":"claude"}',
    SUITE_END_PASS,
  ].join("\n") + "\n";
  const nowMs = 1783116970000;
  const { summary } = parseRun("20260705-capture-9001.ndjson", text, nowMs, nowMs, text.length);

  assert.equal(summary.kind, "capture");
  assert.equal(summary.status, "pass"); // end event outcome still wins
  assert.equal(summary.runner, "claude");
});

test("listRuns: includes a capture-kind run file alongside chz/suite", async () => {
  const dir = await tmpDir();
  const captureFile = JSON.stringify({ ev: "start", ts: 5000, kind: "capture", argv: [], pid: 3, buildType: "Release" }) + "\n" + SUITE_END_PASS + "\n";
  await writeFile(join(dir, "c-capture.ndjson"), captureFile);

  const runs = await listRuns(dir);

  assert.equal(runs.length, 1);
  assert.equal(runs[0].kind, "capture");
  assert.equal(runs[0].status, "pass");
});

test("runnerLabel: maps known runner values and falls back for unknown/missing", () => {
  assert.equal(runnerLabel("ci"), "CI");
  assert.equal(runnerLabel("claude"), "Claude");
  assert.equal(runnerLabel("dashboard"), "you (dashboard)");
  assert.equal(runnerLabel("terminal"), "you (terminal)");
  assert.equal(runnerLabel(undefined), "unknown");
  assert.equal(runnerLabel(""), "unknown");
  assert.equal(runnerLabel("some-future-runner"), "some-future-runner"); // verbatim passthrough
});
