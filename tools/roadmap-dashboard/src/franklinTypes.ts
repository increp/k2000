// Shared by server + frontend. Franklin runs are NDJSON files written by the
// C++ side (chz / suite harnesses); the server scans/parses/compacts them,
// the frontend renders RunSummary/RunDetail.

export type RunKind = "chz" | "suite";

export type RunStatus = "running" | "stalled" | "pass" | "fail" | "error" | "stopped";

export interface RunCheck {
  name: string;
  measured: number;
  expected?: number;
  verdict: "pass" | "fail" | "info";
}

export interface TestEvent {
  name: string;
  sub: string;
  ok: boolean;
  passes: number;
  failures: number;
  messages: string[];
}

export interface ProgressEvent {
  ts: number;
  done: number;
  total: number;
  label: string;
}

export interface RunSummary {
  id: string;
  kind: RunKind;
  startedAt: number;
  status: RunStatus;
  sizeBytes: number;
  done?: number;
  total?: number;
  label?: string;
  durationS?: number;
  model?: string;
  grid?: string;
  tests?: number;
  failed?: number;
  pid?: number;
}

export interface RunDetail extends RunSummary {
  argv?: string[];
  gitSha?: string;
  buildType?: string;
  testsList: TestEvent[];
  checks: RunCheck[];
  progressTail: ProgressEvent[];
}
