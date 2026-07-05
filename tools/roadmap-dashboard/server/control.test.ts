import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, writeFile, readFile, mkdir, chmod, utimes } from "node:fs/promises";
import { spawn } from "node:child_process";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { templates, startRun, stopRun, staleBinaryInfo, pgidOf } from "./control.ts";

async function tmpDir(): Promise<string> {
  return mkdtemp(join(tmpdir(), "franklin-control-"));
}

/** Polls a predicate with short retries instead of a fixed sleep, to keep lifecycle tests fast and unflaky. */
async function waitFor(pred: () => boolean, timeoutMs = 3000, stepMs = 20): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (pred()) return;
    await new Promise((r) => setTimeout(r, stepMs));
  }
  throw new Error("waitFor: timed out");
}

function pidAlive(pid: number): boolean {
  try {
    process.kill(pid, 0);
    return true;
  } catch {
    return false;
  }
}

// --- templates() -----------------------------------------------------------

test("templates: returns the fixed whitelist with suite/suite-disparity/suite-voiceperf/chz", () => {
  const ts = templates();
  const ids = ts.map((t) => t.id).sort();
  assert.deepEqual(ids, ["chz", "suite", "suite-disparity", "suite-voiceperf"]);

  const suite = ts.find((t) => t.id === "suite")!;
  assert.equal(suite.bin, "build/tests/k2000_tests");
  assert.deepEqual(suite.env, {});

  const disparity = ts.find((t) => t.id === "suite-disparity")!;
  assert.equal(disparity.bin, "build/tests/k2000_tests");
  assert.deepEqual(disparity.env, { BERNIE_RUN_DISPARITY: "1" });

  const voiceperf = ts.find((t) => t.id === "suite-voiceperf")!;
  assert.equal(voiceperf.bin, "build/tests/k2000_tests");
  assert.deepEqual(voiceperf.env, { BERNIE_RUN_VOICEPERF: "1" });
});

test("templates: chz args derive from model/grid params (quick appends --quick)", () => {
  const ts = templates({ model: "moog", grid: "quick" });
  const chz = ts.find((t) => t.id === "chz")!;
  assert.equal(chz.bin, "build/tests/k2000_device_characterization");
  assert.deepEqual(chz.args, ["--model", "moog", "--quick"]);
});

test("templates: chz args omit --quick for grid=full", () => {
  const ts = templates({ model: "huggett", grid: "full" });
  const chz = ts.find((t) => t.id === "chz")!;
  assert.deepEqual(chz.args, ["--model", "huggett"]);
});

test("templates: chz defaults args to --model all with no --quick when params omitted", () => {
  const ts = templates();
  const chz = ts.find((t) => t.id === "chz")!;
  assert.deepEqual(chz.args, ["--model", "all"]);
});

// --- startRun: template/param validation (no real spawn expected) ---------

test("startRun: unknown template id rejected without spawning", async () => {
  const rootDir = await tmpDir();
  const result = await startRun(rootDir, "not-a-real-template", {});
  assert.equal(result.ok, false);
  if (!result.ok) assert.match(result.error, /template/i);
});

test("startRun: invalid model for chz rejected", async () => {
  const rootDir = await tmpDir();
  const result = await startRun(rootDir, "chz", { model: "not-a-model", grid: "quick" });
  assert.equal(result.ok, false);
});

test("startRun: invalid grid for chz rejected", async () => {
  const rootDir = await tmpDir();
  const result = await startRun(rootDir, "chz", { model: "moog", grid: "not-a-grid" });
  assert.equal(result.ok, false);
});

// --- startRun: real spawn against a fake fast-exiting binary ---------------

test("startRun: spawns the template binary, writes a sidecar log, returns a live pid", async () => {
  const rootDir = await tmpDir();
  await mkdir(join(rootDir, "build", "tests"), { recursive: true });
  const fakeBin = join(rootDir, "build", "tests", "k2000_tests");
  await writeFile(fakeBin, "#!/bin/sh\necho hello\nexit 0\n");
  await chmod(fakeBin, 0o755);
  const runsDir = join(rootDir, ".franklin", "runs");
  await mkdir(runsDir, { recursive: true });

  const result = await startRun(rootDir, "suite", {});
  assert.equal(result.ok, true);
  if (!result.ok) return;
  assert.equal(typeof result.pid, "number");
  assert.ok(result.pid > 0);

  // Sidecar log file should exist under runsDir, named "<stamp>-<templateId>.log".
  await waitFor(() => !pidAlive(result.pid)); // let the fast echo process finish & flush
  const { readdir } = await import("node:fs/promises");
  const files = await readdir(runsDir);
  const log = files.find((f) => f.endsWith("-suite.log"));
  assert.ok(log, `expected a sidecar log matching *-suite.log in ${files.join(", ")}`);
  const content = await readFile(join(runsDir, log!), "utf8");
  assert.match(content, /hello/);
});

test("startRun: missing binary settles as ok:false instead of crashing the process (unhandled 'error' regression)", async () => {
  const rootDir = await tmpDir();
  // Deliberately do NOT create build/tests/k2000_tests — this is the routine
  // post-`rm -rf build/` state that used to crash the whole server via an
  // unhandled spawn 'error' event (ENOENT), one tick after startRun returned.
  const result = await startRun(rootDir, "suite", {});
  assert.equal(result.ok, false);
  if (!result.ok) assert.match(result.error, /spawn|ENOENT/i);

  // The regression was an UNHANDLED 'error' event crashing the test/server
  // process asynchronously, one tick after this call already returned. There's
  // no direct assertion for "did not crash" — surviving this await plus the
  // tick below, to reach the assertion after it, IS the regression check.
  await new Promise((r) => setTimeout(r, 60));
  assert.ok(true, "process survived the tick where the unhandled 'error' event used to crash it");
});

test("startRun: creates the sidecar .franklin/runs dir when it doesn't exist yet (fresh clone)", async () => {
  const rootDir = await tmpDir();
  await mkdir(join(rootDir, "build", "tests"), { recursive: true });
  const fakeBin = join(rootDir, "build", "tests", "k2000_tests");
  await writeFile(fakeBin, "#!/bin/sh\nexit 0\n");
  await chmod(fakeBin, 0o755);
  // Deliberately do NOT create .franklin/runs — simulates the first-ever
  // dashboard-started run on a fresh clone.

  const result = await startRun(rootDir, "suite", {});
  assert.equal(result.ok, true);

  const runsDir = join(rootDir, ".franklin", "runs");
  const { readdir } = await import("node:fs/promises");
  const files = await readdir(runsDir); // throws if runsDir doesn't exist
  assert.ok(files.some((f) => f.endsWith("-suite.log")), `expected a sidecar log in ${files.join(", ")}`);
});

// --- stopRun: pid-verified stop lifecycle ----------------------------------

test("stopRun: terminates a real spawned process and appends a stopped end event", async () => {
  const rootDir = await tmpDir();
  const runsDir = join(rootDir, ".franklin", "runs");
  await mkdir(runsDir, { recursive: true });

  // A slow script that stopRun must be able to kill.
  const scriptPath = join(rootDir, "slow.sh");
  await writeFile(scriptPath, "#!/bin/sh\nsleep 30\n");
  await chmod(scriptPath, 0o755);

  const child = spawn(scriptPath, [], { detached: true, stdio: "ignore" });
  child.unref();
  const pid = child.pid!;
  assert.ok(pidAlive(pid), "precondition: spawned script must be alive");

  const startTs = Date.now() - 1000; // pretend it started 1s ago
  const runFile = join(runsDir, "20260703-999999-suite-fixture.ndjson");
  const startEvent = { ev: "start", ts: startTs, kind: "suite", argv: [scriptPath], pid, buildType: "Release" };
  await writeFile(runFile, JSON.stringify(startEvent) + "\n");

  const result = await stopRun(rootDir, runsDir, "20260703-999999-suite-fixture.ndjson");
  assert.equal(result.ok, true);

  // Process must actually be gone (SIGTERM should kill a bare `sleep` immediately).
  await waitFor(() => !pidAlive(pid));

  const finalText = await readFile(runFile, "utf8");
  const lines = finalText.split("\n").filter((l) => l.trim().length > 0).map((l) => JSON.parse(l));
  const endEvents = lines.filter((l) => l.ev === "end");
  assert.equal(endEvents.length, 1);
  assert.equal(endEvents[0].outcome, "stopped");
  assert.equal(typeof endEvents[0].durationS, "number");
  assert.ok(endEvents[0].durationS >= 0);
});

test("stopRun: does not append a second end event if one already exists (idempotent, reports ok)", async () => {
  const rootDir = await tmpDir();
  const runsDir = join(rootDir, ".franklin", "runs");
  await mkdir(runsDir, { recursive: true });

  const scriptPath = join(rootDir, "slow2.sh");
  await writeFile(scriptPath, "#!/bin/sh\nsleep 30\n");
  await chmod(scriptPath, 0o755);
  const child = spawn(scriptPath, [], { detached: true, stdio: "ignore" });
  child.unref();
  const pid = child.pid!;

  const runFile = join(runsDir, "already-ended.ndjson");
  const startEvent = { ev: "start", ts: Date.now() - 500, kind: "suite", argv: [scriptPath], pid, buildType: "Release" };
  const endEvent = { ev: "end", ts: Date.now(), outcome: "pass", durationS: 0.5 };
  await writeFile(runFile, JSON.stringify(startEvent) + "\n" + JSON.stringify(endEvent) + "\n");

  const result = await stopRun(rootDir, runsDir, "already-ended.ndjson");
  assert.equal(result.ok, true);

  const finalText = await readFile(runFile, "utf8");
  const lines = finalText.split("\n").filter((l) => l.trim().length > 0).map((l) => JSON.parse(l));
  const endEvents = lines.filter((l) => l.ev === "end");
  assert.equal(endEvents.length, 1, "must not append a second end event");
  assert.equal(endEvents[0].outcome, "pass");

  // Clean up the still-running fixture process (not stopRun's job when already-ended).
  // Group-kill (-pid): the fixture is a #!/bin/sh script that forks `sleep` as a
  // real child rather than exec-replacing itself, and detached:true makes the
  // spawned sh its own process-group leader, so -pid reaps the sleep child too.
  try { process.kill(-pid, "SIGKILL"); } catch { /* already gone */ }
  try { process.kill(pid, "SIGKILL"); } catch { /* already gone */ }
});

test("stopRun: refuses when the recorded pid's cmdline does not match argv[0] basename (pid mismatch)", async () => {
  const rootDir = await tmpDir();
  const runsDir = join(rootDir, ".franklin", "runs");
  await mkdir(runsDir, { recursive: true });

  // process.pid (this test runner's own node process) is alive but its real
  // cmdline is node/node24/etc, never "k2000_tests" — a guaranteed mismatch.
  const runFile = join(runsDir, "mismatch.ndjson");
  const startEvent = {
    ev: "start", ts: Date.now() - 1000, kind: "suite",
    argv: ["./build/tests/k2000_tests"], pid: process.pid, buildType: "Release",
  };
  await writeFile(runFile, JSON.stringify(startEvent) + "\n");

  const result = await stopRun(rootDir, runsDir, "mismatch.ndjson");
  assert.equal(result.ok, false);
  if (!result.ok) assert.match(result.error, /pid/i);

  // Our own process must obviously still be alive — stopRun must not have touched it.
  assert.ok(pidAlive(process.pid));

  // No end event should have been appended on refusal.
  const finalText = await readFile(runFile, "utf8");
  const lines = finalText.split("\n").filter((l) => l.trim().length > 0).map((l) => JSON.parse(l));
  assert.equal(lines.filter((l) => l.ev === "end").length, 0);
});

test("stopRun: dead pid (no /proc entry) is treated as already-stopped success and appends the end event", async () => {
  const rootDir = await tmpDir();
  const runsDir = join(rootDir, ".franklin", "runs");
  await mkdir(runsDir, { recursive: true });

  // Spawn+immediately let a trivial process exit so its pid frees up as "gone".
  const scriptPath = join(rootDir, "quick.sh");
  await writeFile(scriptPath, "#!/bin/sh\nexit 0\n");
  await chmod(scriptPath, 0o755);
  const child = spawn(scriptPath, [], { detached: true, stdio: "ignore" });
  const deadPid = child.pid!;
  await new Promise<void>((resolve) => child.on("exit", () => resolve()));
  await waitFor(() => !pidAlive(deadPid));

  const runFile = join(runsDir, "dead-pid.ndjson");
  const startEvent = { ev: "start", ts: Date.now() - 2000, kind: "suite", argv: [scriptPath], pid: deadPid, buildType: "Release" };
  await writeFile(runFile, JSON.stringify(startEvent) + "\n");

  const result = await stopRun(rootDir, runsDir, "dead-pid.ndjson");
  assert.equal(result.ok, true);

  const finalText = await readFile(runFile, "utf8");
  const lines = finalText.split("\n").filter((l) => l.trim().length > 0).map((l) => JSON.parse(l));
  const endEvents = lines.filter((l) => l.ev === "end");
  assert.equal(endEvents.length, 1);
  assert.equal(endEvents[0].outcome, "stopped");
});

// --- staleBinaryInfo ---------------------------------------------------------

test("staleBinaryInfo: binary older than newest source is stale, newer is not", async () => {
  const rootDir = await tmpDir();
  await mkdir(join(rootDir, "build", "tests"), { recursive: true });
  await mkdir(join(rootDir, "src"), { recursive: true });
  await mkdir(join(rootDir, "tests"), { recursive: true });

  const binPath = join(rootDir, "build", "tests", "k2000_tests");
  await writeFile(binPath, "old-binary");
  const oldTime = new Date(Date.now() - 100_000);
  await utimes(binPath, oldTime, oldTime);

  const srcFile = join(rootDir, "src", "filter.cpp");
  await writeFile(srcFile, "// newer source");
  const newTime = new Date(); // now, newer than the binary
  await utimes(srcFile, newTime, newTime);

  const chzBin = join(rootDir, "build", "tests", "k2000_device_characterization");
  await writeFile(chzBin, "fresh-binary");
  await utimes(chzBin, new Date(Date.now() + 100_000), new Date(Date.now() + 100_000));

  const info = await staleBinaryInfo(rootDir);
  const suiteBinInfo = info.find((i) => i.binary === "build/tests/k2000_tests")!;
  assert.ok(suiteBinInfo, "expected an entry for build/tests/k2000_tests");
  assert.equal(suiteBinInfo.stale, true);
  assert.ok(suiteBinInfo.newestSourceMtimeMs > suiteBinInfo.binaryMtimeMs);

  const chzBinInfo = info.find((i) => i.binary === "build/tests/k2000_device_characterization")!;
  assert.ok(chzBinInfo, "expected an entry for build/tests/k2000_device_characterization");
  assert.equal(chzBinInfo.stale, false);
});

test("staleBinaryInfo: missing binary reports stale:true with binaryMtimeMs 0", async () => {
  const rootDir = await tmpDir();
  await mkdir(join(rootDir, "src"), { recursive: true });
  await writeFile(join(rootDir, "src", "anything.cpp"), "// source, no binary built yet");
  // Note: build/tests/* is never created.

  const info = await staleBinaryInfo(rootDir);
  assert.ok(info.length > 0);
  for (const i of info) {
    assert.equal(i.stale, true);
    assert.equal(i.binaryMtimeMs, 0);
  }
});

test("staleBinaryInfo: only considers .h/.cpp/.cmajor under src/** and tests/**, ignoring other extensions", async () => {
  const rootDir = await tmpDir();
  await mkdir(join(rootDir, "build", "tests"), { recursive: true });
  await mkdir(join(rootDir, "src", "nested"), { recursive: true });

  const binPath = join(rootDir, "build", "tests", "k2000_tests");
  await writeFile(binPath, "binary");
  const binTime = new Date();
  await utimes(binPath, binTime, binTime);

  // A much newer *.md file should NOT count toward newestSourceMtimeMs.
  const ignoredFile = join(rootDir, "src", "nested", "notes.md");
  await writeFile(ignoredFile, "# not a source file for staleness purposes");
  const veryNew = new Date(Date.now() + 1_000_000);
  await utimes(ignoredFile, veryNew, veryNew);

  const cppFile = join(rootDir, "src", "nested", "voice.cpp");
  await writeFile(cppFile, "// counts");
  const olderThanBin = new Date(Date.now() - 100_000);
  await utimes(cppFile, olderThanBin, olderThanBin);

  const info = await staleBinaryInfo(rootDir);
  const suiteBinInfo = info.find((i) => i.binary === "build/tests/k2000_tests")!;
  assert.equal(suiteBinInfo.stale, false, "the .md file must not count as source, so binary should not appear stale");
});

// --- pgidOf / group-leader check -------------------------------------------------

test("pgidOf: detached spawned process identifies itself as its own group leader", async () => {
  const tmpDir_ = await tmpDir();
  const scriptPath = join(tmpDir_, "franklin-detached-fixture.sh");
  // Create a simple script that sleeps so we can query it while alive.
  await writeFile(scriptPath, "#!/bin/sh\nsleep 300\n");
  await chmod(scriptPath, 0o755);
  try {
    const child = spawn(scriptPath, [], { detached: true, stdio: "ignore" });
    child.unref();
    const pid = child.pid!;
    assert.ok(pid > 0);

    // A detached process spawned with detached:true should be its own pgrp leader.
    const pgrp = pgidOf(pid);
    assert.equal(pgrp, pid, `expected pgidOf(${pid}) === ${pid}, got ${pgrp}`);

    // Clean up.
    try { process.kill(-pid, "SIGKILL"); } catch { /* already gone */ }
  } finally {
    try { await import("node:fs/promises").then(m => m.rm(tmpDir_, { recursive: true, force: true })); } catch { /* ignore */ }
  }
});

test("pgidOf: non-detached spawned process does not identify itself as group leader", async () => {
  const tmpDir_ = await tmpDir();
  const scriptPath = join(tmpDir_, "franklin-non-detached-fixture.sh");
  // Create a simple script that sleeps so we can query it while alive.
  await writeFile(scriptPath, "#!/bin/sh\nsleep 300\n");
  await chmod(scriptPath, 0o755);
  const child = spawn(scriptPath, [], { stdio: "ignore" });
  const pid = child.pid!;
  try {
    assert.ok(pid > 0);

    // A non-detached process should NOT be its own group leader (pgidOf(pid) !== pid).
    const pgrp = pgidOf(pid);
    assert.notEqual(pgrp, pid, `expected pgidOf(${pid}) !== ${pid}`);
  } finally {
    // Clean up the spawned process directly (not a group leader, so no -pid).
    try { process.kill(pid, "SIGKILL"); } catch { /* already gone */ }
    try { await import("node:fs/promises").then(m => m.rm(tmpDir_, { recursive: true, force: true })); } catch { /* ignore */ }
  }
});

// --- stopRun: duplicate-end race condition ----------------------------------------

test("stopRun: stopRun is idempotent when called sequentially (end event appended once)", async () => {
  const rootDir = await tmpDir();
  const runsDir = join(rootDir, ".franklin", "runs");
  await mkdir(runsDir, { recursive: true });

  // Create a script that will be "already dead" by the time we call stopRun.
  const scriptPath = join(rootDir, "quick-exit.sh");
  await writeFile(scriptPath, "#!/bin/sh\nexit 0\n");
  await chmod(scriptPath, 0o755);

  const child = spawn(scriptPath, [], { detached: true, stdio: "ignore" });
  const deadPid = child.pid!;
  // Wait for it to actually exit and be reaped from /proc.
  await new Promise<void>((resolve) => child.on("exit", () => resolve()));
  await waitFor(() => !pidAlive(deadPid));

  const runFile = join(runsDir, "duplicate-end-test.ndjson");
  const startEvent = {
    ev: "start",
    ts: Date.now() - 5000,
    kind: "suite",
    argv: [scriptPath],
    pid: deadPid,
    buildType: "Release",
  };
  await writeFile(runFile, JSON.stringify(startEvent) + "\n");

  // Call stopRun twice sequentially on the already-dead process.
  const result1 = await stopRun(rootDir, runsDir, "duplicate-end-test.ndjson");
  assert.equal(result1.ok, true, "first stopRun call must succeed");

  const result2 = await stopRun(rootDir, runsDir, "duplicate-end-test.ndjson");
  assert.equal(result2.ok, true, "second stopRun call must also succeed");

  // File must end with exactly ONE end event.
  const finalText = await readFile(runFile, "utf8");
  const lines = finalText.split("\n").filter((l) => l.trim().length > 0).map((l) => JSON.parse(l));
  const endEvents = lines.filter((l) => l.ev === "end");
  assert.equal(endEvents.length, 1, `expected exactly 1 end event, found ${endEvents.length}`);
  assert.equal(endEvents[0].outcome, "stopped");
});
