import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { parsePrList, parseRunList, getCi, _resetCiCache } from "./ci.ts";

async function tmpDir(): Promise<string> {
  return mkdtemp(join(tmpdir(), "franklin-ci-"));
}

// Real fixture shapes lifted from actual `gh` output on this repo (see brief).
const PR_LIST_JSON = JSON.stringify([
  {
    number: 12,
    title: "feat: live progress…",
    headRefName: "feat/chz-live-progress",
    statusCheckRollup: [
      { conclusion: "SUCCESS", name: "windows", status: "COMPLETED" },
      { conclusion: null, name: "drift-check + suite (linux)", status: "IN_PROGRESS" },
    ],
  },
]);

const RUN_LIST_ELEMENT = {
  displayTitle: "fix: Q27 …",
  status: "completed",
  conclusion: "success",
  url: "https://github.com/increp/k2000/actions/runs/123",
  workflowName: "Build",
};

// --- parsePrList -------------------------------------------------------------

test("parsePrList: maps a PR element to a CiBranch keyed by headRefName", () => {
  const branches = parsePrList(PR_LIST_JSON);
  assert.equal(branches.length, 1);
  const b = branches[0];
  assert.equal(b.ref, "feat/chz-live-progress");
  assert.equal(b.title, "feat: live progress…");
  assert.equal(b.checks.length, 2);
});

test("parsePrList: tolerates a null conclusion in statusCheckRollup", () => {
  const branches = parsePrList(PR_LIST_JSON);
  const checks = branches[0].checks;
  const success = checks.find((c) => c.name === "windows")!;
  assert.equal(success.status, "COMPLETED");
  assert.equal(success.conclusion, "SUCCESS");
  const inProgress = checks.find((c) => c.name === "drift-check + suite (linux)")!;
  assert.equal(inProgress.status, "IN_PROGRESS");
  assert.equal(inProgress.conclusion, null);
});

test("parsePrList: checks carry an empty url (statusCheckRollup entries have none)", () => {
  const branches = parsePrList(PR_LIST_JSON);
  for (const c of branches[0].checks) assert.equal(c.url, "");
});

test("parsePrList: tolerates a missing statusCheckRollup entirely", () => {
  const json = JSON.stringify([
    { number: 5, title: "no rollup yet", headRefName: "feat/no-checks" },
  ]);
  const branches = parsePrList(json);
  assert.equal(branches.length, 1);
  assert.equal(branches[0].ref, "feat/no-checks");
  assert.deepEqual(branches[0].checks, []);
});

test("parsePrList: multiple PR elements produce one CiBranch each", () => {
  const json = JSON.stringify([
    { number: 1, title: "first", headRefName: "a", statusCheckRollup: [] },
    { number: 2, title: "second", headRefName: "b", statusCheckRollup: [] },
  ]);
  const branches = parsePrList(json);
  assert.equal(branches.length, 2);
  assert.deepEqual(branches.map((b) => b.ref), ["a", "b"]);
});

test("parsePrList: empty PR list returns an empty array", () => {
  assert.deepEqual(parsePrList("[]"), []);
});

// --- parseRunList ------------------------------------------------------------

test("parseRunList: collapses run elements into a single CiBranch for main", () => {
  const json = JSON.stringify([RUN_LIST_ELEMENT]);
  const branch = parseRunList(json);
  assert.equal(branch.ref, "main");
  assert.equal(branch.title, "main");
  assert.equal(branch.checks.length, 1);
});

test("parseRunList: each run element maps workflowName+displayTitle to name, url from url, status/conclusion carried", () => {
  const json = JSON.stringify([RUN_LIST_ELEMENT]);
  const branch = parseRunList(json);
  const check = branch.checks[0];
  assert.match(check.name, /Build/);
  assert.match(check.name, /Q27/);
  assert.equal(check.url, "https://github.com/increp/k2000/actions/runs/123");
  assert.equal(check.status, "completed");
  assert.equal(check.conclusion, "success");
});

test("parseRunList: multiple run elements become multiple checks under the one main branch", () => {
  const json = JSON.stringify([
    RUN_LIST_ELEMENT,
    { displayTitle: "chore: drift sweep", status: "completed", conclusion: "success", url: "https://example.com/456", workflowName: "Drift" },
  ]);
  const branch = parseRunList(json);
  assert.equal(branch.checks.length, 2);
  assert.match(branch.checks[1].name, /Drift/);
});

test("parseRunList: empty run list still returns a single main CiBranch with no checks", () => {
  const branch = parseRunList("[]");
  assert.equal(branch.ref, "main");
  assert.deepEqual(branch.checks, []);
});

// --- getCi ---------------------------------------------------------------------

test("getCi: gh unavailable (empty PATH) reports {available:false, branches:[]}", async () => {
  _resetCiCache();
  const rootDir = await tmpDir();
  const emptyPathDir = await tmpDir(); // a tmp dir with nothing executable in it
  const savedPath = process.env.PATH;
  process.env.PATH = emptyPathDir;
  try {
    const payload = await getCi(rootDir);
    assert.equal(payload.available, false);
    assert.deepEqual(payload.branches, []);
    assert.equal(typeof payload.fetchedAt, "number");
  } finally {
    process.env.PATH = savedPath;
  }
});

test("getCi: caches within 60s — a second call with gh still unavailable returns the same fetchedAt", async () => {
  _resetCiCache();
  const rootDir = await tmpDir();
  const emptyPathDir = await tmpDir();
  const savedPath = process.env.PATH;
  process.env.PATH = emptyPathDir;
  try {
    const first = await getCi(rootDir);
    const second = await getCi(rootDir);
    assert.equal(second.fetchedAt, first.fetchedAt, "second call within 60s should reuse the cached payload");
  } finally {
    process.env.PATH = savedPath;
  }
});

test("getCi: _resetCiCache forces a fresh fetch (new fetchedAt)", async () => {
  const rootDir = await tmpDir();
  const emptyPathDir = await tmpDir();
  const savedPath = process.env.PATH;
  process.env.PATH = emptyPathDir;
  try {
    _resetCiCache();
    const first = await getCi(rootDir);
    // Force a distinguishable clock tick so a fresh fetchedAt is provably different.
    await new Promise((r) => setTimeout(r, 5));
    _resetCiCache();
    const second = await getCi(rootDir);
    assert.ok(second.fetchedAt >= first.fetchedAt);
    assert.notEqual(second.fetchedAt, first.fetchedAt, "resetting the cache must force a new fetch timestamp");
  } finally {
    process.env.PATH = savedPath;
  }
});
