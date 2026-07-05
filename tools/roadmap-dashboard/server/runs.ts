import { readdir, readFile, writeFile, rename, stat } from "node:fs/promises";
import { join } from "node:path";
import type { RunSummary, RunDetail, RunKind, RunStatus, TestEvent, RunCheck, ProgressEvent } from "../src/franklinTypes.ts";

export const STALL_MS = 120_000;
export const COMPACT_MARKER = '{"ev":"meta","compacted":true}';
export const MAX_PROGRESS_KEPT = 500;
const PROGRESS_TAIL_KEPT = 50;

// Raw shapes as written by the C++ side (Tasks 1-3). Loosely typed on purpose:
// parseRun's job is to tolerate malformed/truncated lines, so fields are read
// defensively rather than trusted via a strict discriminated union.
interface RawEvent {
  ev?: string;
  ts?: number;
  kind?: RunKind;
  argv?: string[];
  pid?: number;
  buildType?: string;
  model?: string;
  grid?: string;
  gitSha?: string;
  done?: number;
  total?: number;
  label?: string;
  name?: string;
  sub?: string;
  ok?: boolean;
  passes?: number;
  failures?: number;
  messages?: string[];
  outcome?: RunStatus;
  durationS?: number;
  tests?: number;
  failed?: number;
  checks?: RunCheck[];
  measured?: number;
  expected?: number;
  verdict?: RunCheck["verdict"];
}

function parseLines(text: string): RawEvent[] {
  const events: RawEvent[] = [];
  for (const line of text.split("\n")) {
    const trimmed = line.trim();
    if (trimmed.length === 0) continue;
    try {
      events.push(JSON.parse(trimmed) as RawEvent);
    } catch {
      // Malformed/truncated line (e.g. writer killed mid-flush) — skip it.
      continue;
    }
  }
  return events;
}

/** Pure: parses one run's NDJSON text into the summary/detail views. No I/O. */
export function parseRun(id: string, text: string, mtimeMs: number, nowMs: number, sizeBytes: number): { summary: RunSummary; detail: RunDetail } {
  const events = parseLines(text);

  let startedAt = mtimeMs;
  let kind: RunKind = "suite";
  let argv: string[] | undefined;
  let pid: number | undefined;
  let buildType: string | undefined;
  let model: string | undefined;
  let grid: string | undefined;
  let gitSha: string | undefined;

  const progress: ProgressEvent[] = [];
  const testsList: TestEvent[] = [];
  const checks: RunCheck[] = [];

  let endOutcome: RunStatus | undefined;
  let durationS: number | undefined;
  let tests: number | undefined;
  let failed: number | undefined;

  for (const e of events) {
    switch (e.ev) {
      case "start":
        if (typeof e.ts === "number") startedAt = e.ts;
        if (e.kind === "chz" || e.kind === "suite") kind = e.kind;
        argv = e.argv;
        pid = e.pid;
        buildType = e.buildType;
        model = e.model;
        grid = e.grid;
        gitSha = e.gitSha;
        break;
      case "progress":
        if (typeof e.done === "number" && typeof e.total === "number" && typeof e.label === "string" && typeof e.ts === "number") {
          progress.push({ ts: e.ts, done: e.done, total: e.total, label: e.label });
        }
        break;
      case "test":
        if (typeof e.name === "string" && typeof e.sub === "string" && typeof e.ok === "boolean") {
          testsList.push({
            name: e.name, sub: e.sub, ok: e.ok,
            passes: e.passes ?? 0, failures: e.failures ?? 0, messages: e.messages ?? [],
          });
        }
        break;
      case "end":
        endOutcome = e.outcome;
        durationS = e.durationS;
        tests = e.tests;
        failed = e.failed;
        if (Array.isArray(e.checks)) {
          for (const c of e.checks) {
            if (typeof c.name === "string" && typeof c.measured === "number" && typeof c.verdict === "string") {
              checks.push({ name: c.name, measured: c.measured, expected: c.expected, verdict: c.verdict });
            }
          }
        }
        break;
      default:
        break;
    }
  }

  const status: RunStatus = endOutcome ?? (nowMs - mtimeMs > STALL_MS ? "stalled" : "running");
  const lastProgress = progress.at(-1);

  const summary: RunSummary = {
    id, kind, startedAt, status, sizeBytes,
    done: lastProgress?.done, total: lastProgress?.total, label: lastProgress?.label,
    durationS, model, grid, tests, failed, pid,
  };

  const detail: RunDetail = {
    ...summary,
    argv, gitSha, buildType,
    testsList, checks,
    progressTail: progress.slice(-PROGRESS_TAIL_KEPT),
  };

  return { summary, detail };
}

/** Scans dir for *.ndjson run files, newest (by startedAt) first. */
export async function listRuns(dir: string): Promise<RunSummary[]> {
  const entries = await readdir(dir).catch(() => [] as string[]);
  const files = entries.filter((f) => f.endsWith(".ndjson"));
  const nowMs = Date.now();
  const summaries: RunSummary[] = [];
  for (const file of files) {
    const filePath = join(dir, file);
    const [text, st] = await Promise.all([readFile(filePath, "utf8"), stat(filePath)]);
    const { summary } = parseRun(file, text, st.mtimeMs, nowMs, st.size);
    summaries.push(summary);
  }
  summaries.sort((a, b) => b.startedAt - a.startedAt);
  return summaries;
}

/** id must be a bare filename: no path separators, no "..". Returns null otherwise or if missing. */
export async function readRun(dir: string, id: string): Promise<RunDetail | null> {
  if (id.includes("/") || id.includes("\\") || id.includes("..")) return null;
  const filePath = join(dir, id);
  try {
    const [text, st] = await Promise.all([readFile(filePath, "utf8"), stat(filePath)]);
    const { detail } = parseRun(id, text, st.mtimeMs, Date.now(), st.size);
    return detail;
  } catch {
    return null;
  }
}

/** Downsamples an already-parsed progress list to <=MAX_PROGRESS_KEPT, evenly indexed, order preserved. */
function downsampleProgress(progress: RawEvent[]): RawEvent[] {
  if (progress.length <= MAX_PROGRESS_KEPT) return progress;
  const indices = new Set<number>();
  // Reserve space for the final event by sampling up to MAX_PROGRESS_KEPT - 1
  const step = progress.length / (MAX_PROGRESS_KEPT - 1);
  for (let i = 0; i < MAX_PROGRESS_KEPT - 1; i++) {
    indices.add(Math.floor(i * step));
  }
  // Always include the final event
  indices.add(progress.length - 1);

  // Sort indices and collect in order
  const sortedIndices = Array.from(indices).sort((a, b) => a - b);
  return sortedIndices.map(i => progress[i]);
}

/**
 * Compacts every finished-but-uncompacted *.ndjson in dir: marker + start +
 * downsampled progress + all test lines + end, written atomically. Returns
 * the number of files compacted. Idempotent (marker line makes a file skip
 * on the next pass).
 */
export async function compactFinished(dir: string): Promise<number> {
  const entries = await readdir(dir).catch(() => [] as string[]);
  const files = entries.filter((f) => f.endsWith(".ndjson"));
  let compacted = 0;

  for (const file of files) {
    const filePath = join(dir, file);
    const text = await readFile(filePath, "utf8");
    const rawLines = text.split("\n").filter((l) => l.trim().length > 0);
    if (rawLines.length === 0) continue;
    if (rawLines[0].trim() === COMPACT_MARKER) continue; // already compacted

    let hasEnd = false;
    let startLine: string | undefined;
    let endLine: string | undefined;
    const progressLines: RawEvent[] = [];
    const testLines: string[] = [];

    for (const line of rawLines) {
      let e: RawEvent;
      try {
        e = JSON.parse(line) as RawEvent;
      } catch {
        continue; // malformed/truncated — dropped, same as parseRun
      }
      if (e.ev === "start") startLine = line;
      else if (e.ev === "progress") progressLines.push(e);
      else if (e.ev === "test") testLines.push(line);
      else if (e.ev === "end") { hasEnd = true; endLine = line; }
    }

    if (!hasEnd) continue; // only compact finished runs

    const kept = downsampleProgress(progressLines);
    const out: string[] = [COMPACT_MARKER];
    if (startLine) out.push(startLine);
    for (const p of kept) out.push(JSON.stringify(p));
    out.push(...testLines);
    if (endLine) out.push(endLine);

    const tmpPath = `${filePath}.tmp`;
    await writeFile(tmpPath, out.join("\n") + "\n", "utf8");
    await rename(tmpPath, filePath);
    compacted++;
  }

  return compacted;
}
