import { spawn, execFile } from "node:child_process";
import { readFileSync } from "node:fs";
import { open, readFile, writeFile, appendFile, readdir, stat, mkdir } from "node:fs/promises";
import { join, basename } from "node:path";

export interface Template {
  id: string;
  label: string;
  bin: string;
  args: string[];
  env: Record<string, string>;
}

export interface StaleInfo {
  binary: string;
  stale: boolean;
  binaryMtimeMs: number;
  newestSourceMtimeMs: number;
}

const CHZ_BIN = "build/tests/k2000_device_characterization";
const SUITE_BIN = "build/tests/k2000_tests";
const VALID_MODELS = new Set(["moog", "huggett", "all"]);
const VALID_GRIDS = new Set(["quick", "full", "spd", "osalias", "rates", "largesig", "deep"]);
const SOURCE_EXTS = new Set([".h", ".cpp", ".cmajor"]);
const SIGKILL_ESCALATION_MS = 5000;

/** Fixed template whitelist. chz's args are the only part derived from params. */
export function templates(params?: { model?: string; grid?: string }): Template[] {
  const model = params?.model ?? "all";
  const grid = params?.grid ?? "full";
  const chzArgs = ["--model", model, "--grid", grid];

  return [
    { id: "suite", label: "Full suite", bin: SUITE_BIN, args: [], env: {} },
    { id: "suite-disparity", label: "Suite (disparity sweep)", bin: SUITE_BIN, args: [], env: { BERNIE_RUN_DISPARITY: "1" } },
    { id: "suite-voiceperf", label: "Suite (voice perf)", bin: SUITE_BIN, args: [], env: { BERNIE_RUN_VOICEPERF: "1" } },
    { id: "chz", label: "Device characterization", bin: CHZ_BIN, args: chzArgs, env: {} },
  ];
}

function timestampStamp(d = new Date()): string {
  const pad = (n: number, w = 2) => String(n).padStart(w, "0");
  return (
    `${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}-` +
    `${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}${pad(d.getMilliseconds(), 3)}`
  );
}

async function gitSha(rootDir: string): Promise<string> {
  return new Promise((resolve) => {
    execFile("git", ["rev-parse", "HEAD"], { cwd: rootDir }, (err, stdout) => {
      resolve(err ? "" : stdout.trim());
    });
  });
}

/** Spawns a whitelisted template's binary, detached, with output piped to a sidecar log. */
export async function startRun(
  rootDir: string,
  templateId: string,
  params: { model?: string; grid?: string }
): Promise<{ ok: true; pid: number } | { ok: false; error: string }> {
  if (params.model !== undefined && !VALID_MODELS.has(params.model)) {
    return { ok: false, error: `invalid model: ${params.model}` };
  }
  if (params.grid !== undefined && !VALID_GRIDS.has(params.grid)) {
    return { ok: false, error: `invalid grid: ${params.grid}` };
  }

  const t = templates(params).find((x) => x.id === templateId);
  if (!t) return { ok: false, error: `unknown template: ${templateId}` };

  const runsDir = join(rootDir, ".franklin", "runs");
  await mkdir(runsDir, { recursive: true });
  const stamp = timestampStamp();
  const logPath = join(runsDir, `${stamp}-${t.id}.log`);
  const sha = await gitSha(rootDir);

  const fd = await open(logPath, "a");
  try {
    const child = spawn(t.bin, t.args, {
      cwd: rootDir,
      env: { ...process.env, ...t.env, BERNIE_GIT_SHA: sha, BERNIE_RUNNER: "dashboard" },
      detached: true,
      stdio: ["ignore", fd.fd, fd.fd],
    });

    // Settle the spawn outcome explicitly. spawn() emits 'error' asynchronously
    // (e.g. ENOENT when the binary is missing, routine after `rm -rf build/`);
    // without a listener that event is unhandled and crashes the whole server
    // one tick after we'd otherwise have returned. Racing 'spawn' vs 'error'
    // here means startRun never lets 'error' escape unhandled.
    const outcome = await new Promise<{ ok: true; pid: number } | { ok: false; error: string }>((resolve) => {
      child.once("spawn", () => resolve({ ok: true, pid: child.pid! }));
      child.once("error", (e) => resolve({ ok: false, error: `spawn failed: ${(e as Error).message}` }));
    });

    if (outcome.ok) child.unref();
    return outcome;
  } finally {
    await fd.close();
  }
}


/**
 * Parses /proc/<pid>/stat to extract the process group id (pgid).
 * Returns null if /proc/<pid>/stat is unreadable or malformed.
 * The pgrp field is stat field 5 overall = index 2 of the fields after the final ')' in comm.
 */
export function pgidOf(pid: number): number | null {
  try {
    const statPath = `/proc/${pid}/stat`;
    const data = readFileSync(statPath, "utf8");
    // Find the last ')' to skip the comm field (which can contain spaces/parens).
    const lastParen = data.lastIndexOf(")");
    if (lastParen === -1) return null;
    // Split the remainder on whitespace and extract field index 2 (pgrp, after the ')').
    const remainder = data.slice(lastParen + 1).trim().split(/\s+/);
    if (remainder.length < 3) return null;
    const pgrp = parseInt(remainder[2], 10);
    return isNaN(pgrp) ? null : pgrp;
  } catch {
    return null;
  }
}

/**
 * Signals pid's whole process group (negative pid), not just pid itself.
 * startRun always spawns with detached:true, which makes the child its own
 * group leader (pgid === pid) — so this also reaps any descendants a
 * whitelisted binary forks (e.g. a shell-script harness that forks a real
 * worker instead of exec-replacing itself). Falls back to signaling the bare
 * pid if the process is not its own group leader or if group-kill fails
 * for any reason.
 */
function signalProcessTree(pid: number, signal: NodeJS.Signals): void {
  // Check if pid is its own group leader; if so, use group kill.
  const pgrp = pgidOf(pid);
  if (pgrp === pid) {
    try {
      process.kill(-pid, signal);
      return;
    } catch {
      // Group kill failed despite pgrp === pid (race condition edge case);
      // fall back to signaling the bare pid.
    }
  }
  // pid is not the group leader, or we couldn't read /proc/<pid>/stat;
  // signal the bare pid only.
  try {
    process.kill(pid, signal);
  } catch {
    /* already gone */
  }
}

async function readCmdline(pid: number): Promise<string | null> {
  try {
    const raw = await readFile(`/proc/${pid}/cmdline`, "utf8");
    return raw.replace(/\0/g, " ");
  } catch {
    return null; // no /proc entry -> process already dead
  }
}

// Defensive shape for JSON.parse'd lines: fields read loosely, same rationale as runs.ts's RawEvent.
interface RawRunFileEvent {
  ev?: string;
  ts?: number;
  pid?: number;
  argv?: string[];
}

interface RunFileEvents {
  startEvent: { ts: number; pid: number; argv: string[] } | null;
  hasEnd: boolean;
}

function parseRunFile(text: string): RunFileEvents {
  let startEvent: RunFileEvents["startEvent"] = null;
  let hasEnd = false;
  for (const line of text.split("\n")) {
    const trimmed = line.trim();
    if (trimmed.length === 0) continue;
    let e: RawRunFileEvent;
    try {
      e = JSON.parse(trimmed) as RawRunFileEvent;
    } catch {
      continue;
    }
    if (e.ev === "start" && typeof e.ts === "number" && typeof e.pid === "number" && Array.isArray(e.argv)) {
      startEvent = { ts: e.ts, pid: e.pid, argv: e.argv };
    } else if (e.ev === "end") {
      hasEnd = true;
    }
  }
  return { startEvent, hasEnd };
}

/** Stops a run: verifies the recorded pid's /proc cmdline still matches argv[0] before signaling. */
export async function stopRun(
  rootDir: string,
  runsDir: string,
  id: string
): Promise<{ ok: true } | { ok: false; error: string }> {
  const filePath = join(runsDir, id);
  let text: string;
  try {
    text = await readFile(filePath, "utf8");
  } catch {
    return { ok: false, error: "run file not found" };
  }

  const { startEvent, hasEnd } = parseRunFile(text);
  if (!startEvent) return { ok: false, error: "run file has no start event" };
  if (hasEnd) return { ok: true }; // already finished; nothing to do, report success

  const { pid, argv, ts } = startEvent;
  const expectedBasename = basename(argv[0] ?? "");

  const cmdline = await readCmdline(pid);
  if (cmdline !== null) {
    // Process exists: refuse unless its real cmdline still contains our recorded binary name.
    if (!expectedBasename || !cmdline.includes(expectedBasename)) {
      return { ok: false, error: "pid mismatch: recorded process no longer matches" };
    }
    signalProcessTree(pid, "SIGTERM");

    const timer = setTimeout(() => {
      // Always attempt escalation; escalation relies on signalProcessTree's internal error swallowing.
      signalProcessTree(pid, "SIGKILL");
    }, SIGKILL_ESCALATION_MS);
    timer.unref();
  }
  // else: no /proc entry -> already dead -> treat as success, still append the end event.

  const durationS = (Date.now() - ts) / 1000;
  const endEvent = { ev: "end", ts: Date.now(), outcome: "stopped", durationS };

  // Re-read the file and check again for a duplicate end event (race condition guard).
  try {
    const freshText = await readFile(filePath, "utf8");
    const { hasEnd: freshHasEnd } = parseRunFile(freshText);
    if (freshHasEnd) {
      // Another caller already appended an end event; skip append and report success.
      return { ok: true };
    }
  } catch {
    // If re-read fails, proceed with the append anyway (file may be in flux).
  }

  await appendFile(filePath, JSON.stringify(endEvent) + "\n");

  return { ok: true };
}

async function newestMtimeUnder(dir: string): Promise<number> {
  let newest = 0;
  let entries;
  try {
    entries = await readdir(dir, { withFileTypes: true });
  } catch {
    return 0;
  }
  for (const entry of entries) {
    const full = join(dir, entry.name);
    if (entry.isDirectory()) {
      const sub = await newestMtimeUnder(full);
      if (sub > newest) newest = sub;
    } else if (entry.isFile()) {
      const dot = entry.name.lastIndexOf(".");
      const ext = dot >= 0 ? entry.name.slice(dot) : "";
      if (!SOURCE_EXTS.has(ext)) continue;
      const st = await stat(full);
      if (st.mtimeMs > newest) newest = st.mtimeMs;
    }
  }
  return newest;
}

/** Compares each whitelisted binary's mtime against the newest source file under src/** and tests/**. */
export async function staleBinaryInfo(rootDir: string): Promise<StaleInfo[]> {
  const [srcNewest, testsNewest] = await Promise.all([
    newestMtimeUnder(join(rootDir, "src")),
    newestMtimeUnder(join(rootDir, "tests")),
  ]);
  const newestSourceMtimeMs = Math.max(srcNewest, testsNewest);

  const binaries = [...new Set(templates().map((t) => t.bin))];
  const results: StaleInfo[] = [];
  for (const binary of binaries) {
    let binaryMtimeMs = 0;
    try {
      const st = await stat(join(rootDir, binary));
      binaryMtimeMs = st.mtimeMs;
    } catch {
      binaryMtimeMs = 0; // missing binary
    }
    const stale = binaryMtimeMs === 0 || newestSourceMtimeMs > binaryMtimeMs;
    results.push({ binary, stale, binaryMtimeMs, newestSourceMtimeMs });
  }
  return results;
}
