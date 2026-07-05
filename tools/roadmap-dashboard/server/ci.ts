import { execFile } from "node:child_process";
import { promisify } from "node:util";

const execFileP = promisify(execFile);

const CACHE_MS = 60_000;

export interface CiCheck {
  name: string;
  status: string;
  conclusion: string | null;
  url: string;
}

export interface CiBranch {
  ref: string;
  title: string;
  checks: CiCheck[];
}

export interface CiPayload {
  available: boolean;
  fetchedAt: number;
  branches: CiBranch[];
}

// Loosely-typed raw shapes: gh's JSON is external input, read defensively
// rather than trusted via a strict type (same rationale as runs.ts's RawEvent).
interface RawRollupCheck {
  name?: string;
  status?: string;
  conclusion?: string | null;
}

interface RawPrElement {
  number?: number;
  title?: string;
  headRefName?: string;
  statusCheckRollup?: RawRollupCheck[];
}

interface RawRunElement {
  displayTitle?: string;
  status?: string;
  conclusion?: string | null;
  url?: string;
  workflowName?: string;
}

/** Pure: parses `gh pr list --json number,title,headRefName,statusCheckRollup` output into one CiBranch per PR. */
export function parsePrList(json: string): CiBranch[] {
  const elements = JSON.parse(json) as RawPrElement[];
  return elements.map((pr) => {
    const rollup = Array.isArray(pr.statusCheckRollup) ? pr.statusCheckRollup : [];
    const checks: CiCheck[] = rollup.map((c) => ({
      name: c.name ?? "",
      status: c.status ?? "",
      conclusion: c.conclusion ?? null,
      url: "",
    }));
    return { ref: pr.headRefName ?? "", title: pr.title ?? "", checks };
  });
}

/** Pure: parses `gh run list --branch main ...` output into a single CiBranch for main (one check per run). */
export function parseRunList(json: string): CiBranch {
  const elements = JSON.parse(json) as RawRunElement[];
  const checks: CiCheck[] = elements.map((r) => ({
    name: `${r.workflowName ?? ""}: ${r.displayTitle ?? ""}`,
    status: r.status ?? "",
    conclusion: r.conclusion ?? null,
    url: r.url ?? "",
  }));
  return { ref: "main", title: "main", checks };
}

let cache: { fetchedAt: number; payload: CiPayload } | null = null;

/** Test-only: clears the 60s in-memory cache so the next getCi() call re-fetches. */
export function _resetCiCache(): void {
  cache = null;
}

/** Shells out to `gh` for open-PR checks + recent main-branch runs. 60s cached; any failure -> {available:false}. */
export async function getCi(rootDir: string): Promise<CiPayload> {
  const now = Date.now();
  if (cache && now - cache.fetchedAt < CACHE_MS) return cache.payload;

  try {
    const [prResult, runResult] = await Promise.all([
      execFileP("gh", ["pr", "list", "--json", "number,title,headRefName,statusCheckRollup"], { cwd: rootDir }),
      execFileP("gh", ["run", "list", "--branch", "main", "--limit", "5", "--json", "displayTitle,status,conclusion,url,workflowName"], { cwd: rootDir }),
    ]);
    const prBranches = parsePrList(prResult.stdout);
    const runBranch = parseRunList(runResult.stdout);
    const payload: CiPayload = { available: true, fetchedAt: now, branches: [...prBranches, runBranch] };
    cache = { fetchedAt: now, payload };
    return payload;
  } catch {
    const payload: CiPayload = { available: false, fetchedAt: now, branches: [] };
    cache = { fetchedAt: now, payload };
    return payload;
  }
}
