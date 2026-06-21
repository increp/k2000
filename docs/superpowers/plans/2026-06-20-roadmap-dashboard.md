# Roadmap Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a compact, project-only roadmap dashboard (between Jira and ClickUp) backed by a git-tracked `roadmap.json` single source of truth, and re-engineer `docs/roadmap/phases.md` to match.

**Architecture:** A tiny zero-framework Node HTTP server reads/writes `tools/roadmap-dashboard/roadmap.json` and serves a TypeScript UI bundled by esbuild. The UI renders the roadmap tree to HTML strings (pure functions), mutates an in-memory document via pure transforms, persists via `PUT /api/roadmap`, and hands roadmap items to Claude Code for task decomposition through a request-file + clipboard loop.

**Tech Stack:** Node 24 (native TypeScript type-stripping, built-in `node:test` runner, `node:http`), esbuild (only runtime/dev dependency), vanilla DOM (no UI framework).

## Global Constraints

- **Node runtime:** Node 24+ — relies on native TS type-stripping (`node server/server.ts` and `node --test *.test.ts` run TypeScript directly). All TS must be **erasable syntax only**: no `enum`, no `namespace`, no constructor parameter properties. Use `interface`, `type` aliases, and union string literals.
- **Dependencies:** esbuild is the **only** dependency. No UI framework, no test framework, no Anthropic SDK.
- **Build isolation:** the tool lives entirely under `tools/roadmap-dashboard/` and is **never** referenced by the C++ `CMakeLists.txt`. Do not touch the CMake build.
- **Source of truth:** `tools/roadmap-dashboard/roadmap.json` is canonical for version/status/ordering. After this plan, no other file (including `phases.md`) may carry version/status/ordering tables.
- **Persistence safety:** all writes to `roadmap.json` are atomic (write temp file, then `rename`) so a crash cannot truncate it.
- **Claude handoff:** the decompose feature is a **file + clipboard handoff** to the existing Claude Code session. No automated Anthropic API calls, ever.
- **Product naming:** the synthesizer is **Bernie**; its FX section is **Ricky**. Use these names in seed copy.
- **Git-ignored:** `tools/roadmap-dashboard/node_modules/`, `tools/roadmap-dashboard/dist/`, and `tools/roadmap-dashboard/requests/` are git-ignored. `roadmap.json` IS committed.

---

### Task 1: Scaffold + shared types + progress roll-up

**Files:**
- Create: `tools/roadmap-dashboard/package.json`
- Create: `tools/roadmap-dashboard/tsconfig.json`
- Create: `tools/roadmap-dashboard/src/types.ts`
- Create: `tools/roadmap-dashboard/src/progress.ts`
- Test: `tools/roadmap-dashboard/src/progress.test.ts`
- Modify: `.gitignore` (append tool ignores)

**Interfaces:**
- Produces: the `RoadmapDoc`, `RoadmapItem`, `Status`, `ItemKind`, `Tag`, `Thread` types (consumed by every later task); `computeProgress(item: RoadmapItem): number` returning 0–100.

- [ ] **Step 1: Create the package manifest**

`tools/roadmap-dashboard/package.json`:
```json
{
  "name": "bernie-roadmap-dashboard",
  "version": "1.0.0",
  "private": true,
  "type": "module",
  "description": "Compact project-only roadmap dashboard for Bernie",
  "scripts": {
    "build": "esbuild src/main.ts --bundle --outfile=dist/bundle.js --format=iife --target=es2022",
    "dashboard": "npm run build && node server/server.ts",
    "test": "node --test"
  },
  "devDependencies": {
    "esbuild": "^0.24.0"
  }
}
```

- [ ] **Step 2: Create the TypeScript config**

`tools/roadmap-dashboard/tsconfig.json`:
```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "ESNext",
    "moduleResolution": "bundler",
    "strict": true,
    "verbatimModuleSyntax": true,
    "noEmit": true,
    "lib": ["ES2022", "DOM", "DOM.Iterable"],
    "skipLibCheck": true
  },
  "include": ["src", "server"]
}
```

- [ ] **Step 3: Add git-ignore entries**

Append to `.gitignore`:
```
# Roadmap dashboard tool (data file IS committed; build/deps/requests are not)
tools/roadmap-dashboard/node_modules/
tools/roadmap-dashboard/dist/
tools/roadmap-dashboard/requests/
```

- [ ] **Step 4: Define shared types**

`tools/roadmap-dashboard/src/types.ts`:
```ts
export type Status =
  | "shipped"
  | "in-progress"
  | "specced"
  | "planned"
  | "tentative"
  | "blocked";

export type ItemKind = "version" | "point" | "feature" | "task";

export type Tag = "keystone" | "gate" | "conditional" | "continuous";

export interface RoadmapItem {
  id: string;
  title: string;
  status: Status;
  kind: ItemKind;
  summary?: string;
  notes?: string;
  tags?: Tag[];
  shipped?: string;        // ISO date
  firmness?: "firm" | "tentative";
  specLinks?: string[];
  progressOverride?: number; // 0..100
  children?: RoadmapItem[];
}

export interface Thread {
  id: string;
  title: string;
  summary?: string;
}

export interface RoadmapMeta {
  product: string;
  tagline: string;
  currentVersion: string;
  nextStep: string;
  lastUpdated: string;     // ISO date
  schemaVersion: number;
}

export interface RoadmapDoc {
  meta: RoadmapMeta;
  items: RoadmapItem[];
  continuousThreads: Thread[];
}

export const SCHEMA_VERSION = 1;
```

- [ ] **Step 5: Write the failing test for the progress roll-up**

`tools/roadmap-dashboard/src/progress.test.ts`:
```ts
import { test } from "node:test";
import assert from "node:assert/strict";
import { computeProgress } from "./progress.ts";
import type { RoadmapItem } from "./types.ts";

function leaf(status: RoadmapItem["status"]): RoadmapItem {
  return { id: "x", title: "x", status, kind: "task" };
}

test("leaf shipped = 100", () => {
  assert.equal(computeProgress(leaf("shipped")), 100);
});

test("leaf in-progress = 50", () => {
  assert.equal(computeProgress(leaf("in-progress")), 50);
});

test("leaf planned = 0", () => {
  assert.equal(computeProgress(leaf("planned")), 0);
});

test("override wins over rollup", () => {
  const item: RoadmapItem = {
    id: "v", title: "v", status: "in-progress", kind: "version",
    progressOverride: 73,
    children: [leaf("planned"), leaf("planned")],
  };
  assert.equal(computeProgress(item), 73);
});

test("rollup = shipped leaves / total leaves", () => {
  const item: RoadmapItem = {
    id: "v", title: "v", status: "in-progress", kind: "version",
    children: [leaf("shipped"), leaf("shipped"), leaf("planned"), leaf("planned")],
  };
  assert.equal(computeProgress(item), 50);
});

test("nested leaves are counted across depth", () => {
  const item: RoadmapItem = {
    id: "v", title: "v", status: "in-progress", kind: "version",
    children: [
      { id: "p", title: "p", status: "in-progress", kind: "point",
        children: [leaf("shipped"), leaf("planned")] },
      leaf("shipped"),
    ],
  };
  // leaves: shipped, planned, shipped -> 2/3 = 66.67 -> rounded 67
  assert.equal(computeProgress(item), 67);
});

test("empty children falls back to own status", () => {
  const item: RoadmapItem = {
    id: "v", title: "v", status: "shipped", kind: "version", children: [],
  };
  assert.equal(computeProgress(item), 100);
});
```

- [ ] **Step 6: Run the test to verify it fails**

Run: `cd tools/roadmap-dashboard && node --test`
Expected: FAIL — `Cannot find module './progress.ts'` (not yet created).

- [ ] **Step 7: Implement the progress roll-up**

`tools/roadmap-dashboard/src/progress.ts`:
```ts
import type { RoadmapItem } from "./types.ts";

function statusToPercent(status: RoadmapItem["status"]): number {
  if (status === "shipped") return 100;
  if (status === "in-progress") return 50;
  return 0;
}

function collectLeaves(item: RoadmapItem, out: RoadmapItem[]): void {
  if (!item.children || item.children.length === 0) {
    out.push(item);
    return;
  }
  for (const child of item.children) collectLeaves(child, out);
}

/** 0..100. Manual override wins; otherwise roll up from leaf descendants. */
export function computeProgress(item: RoadmapItem): number {
  if (typeof item.progressOverride === "number") {
    return Math.max(0, Math.min(100, item.progressOverride));
  }
  if (!item.children || item.children.length === 0) {
    return statusToPercent(item.status);
  }
  const leaves: RoadmapItem[] = [];
  collectLeaves(item, leaves);
  if (leaves.length === 0) return statusToPercent(item.status);
  const shipped = leaves.filter((l) => l.status === "shipped").length;
  return Math.round((shipped / leaves.length) * 100);
}
```

- [ ] **Step 8: Run the test to verify it passes**

Run: `cd tools/roadmap-dashboard && node --test`
Expected: PASS — 7 tests pass.

- [ ] **Step 9: Verify the build toolchain installs and runs**

Run: `cd tools/roadmap-dashboard && npm install && npm run build`
Expected: esbuild installs; build fails only because `src/main.ts` does not exist yet — that is fine for this task (created in Task 4). If you want a green build now, skip; otherwise it is expected to error on the missing entry point.

- [ ] **Step 10: Commit**

```bash
git add tools/roadmap-dashboard/package.json tools/roadmap-dashboard/tsconfig.json \
        tools/roadmap-dashboard/src/types.ts tools/roadmap-dashboard/src/progress.ts \
        tools/roadmap-dashboard/src/progress.test.ts .gitignore
git commit -m "feat(roadmap): scaffold dashboard tool + types + progress rollup"
```

---

### Task 2: Seed `roadmap.json` + store (load/save with atomic write + schema guard)

**Files:**
- Create: `tools/roadmap-dashboard/roadmap.json`
- Create: `tools/roadmap-dashboard/server/store.ts`
- Test: `tools/roadmap-dashboard/server/store.test.ts`

**Interfaces:**
- Consumes: `RoadmapDoc`, `SCHEMA_VERSION` from `../src/types.ts`.
- Produces: `loadRoadmap(path: string): Promise<RoadmapDoc>`; `saveRoadmap(path: string, doc: RoadmapDoc): Promise<void>` (atomic); `validateDoc(value: unknown): RoadmapDoc` (throws `Error` on invalid shape/schema).

- [ ] **Step 1: Author the seed roadmap data**

`tools/roadmap-dashboard/roadmap.json` — transcribe the spec §3 verbatim. Use this exact content:
```json
{
  "meta": {
    "product": "Bernie",
    "tagline": "A K2061/K2088-class VAST engine bracketed by a constant Summit voice",
    "currentVersion": "5",
    "nextStep": "v5.1 — Q17 click-free model hot-swap",
    "lastUpdated": "2026-06-20",
    "schemaVersion": 1
  },
  "items": [
    { "id": "v1", "title": "v1 — Skeleton end-to-end", "status": "shipped", "kind": "version", "shipped": "2026-05-30", "summary": "1 osc, 2-slot DSP chain, ADSR, 8-voice, plain UI." },
    { "id": "v2", "title": "v2 — Layer abstraction", "status": "shipped", "kind": "version", "shipped": "2026-06-14", "summary": "Voice split into per-note runtime + Layer owning DSP + ParamSnapshot." },
    { "id": "v3", "title": "v3 — Algorithm abstraction", "status": "shipped", "kind": "version", "shipped": "2026-06-15", "summary": "Selectable algorithm = ordered walk through a per-Layer block palette." },
    { "id": "v4", "title": "v4 — Multi-Layer Programs", "status": "shipped", "kind": "version", "shipped": "2026-06-16", "summary": "Program holds 2 Layers; shared voice pool; Layer/Split/Dual routing." },
    { "id": "v4.5", "title": "v4.5 — K2061 re-positioning + Summit UI foundation", "status": "in-progress", "kind": "version", "shipped": "2026-06-19", "summary": "Engine re-positioned to K2061/K2088 VAST + constant Summit spine; UI foundation. KB OCR fix outstanding.", "children": [
      { "id": "v4.5-kb-ocr", "title": "Pirkle synth-book OCR fix in k2000-kb", "status": "planned", "kind": "task", "summary": "PDF text layer is garbled mojibake; needs an OCR fallback in kb-build." }
    ] },
    { "id": "v5", "title": "v5 — The Constant Summit Spine", "status": "in-progress", "kind": "version", "tags": ["keystone"], "firmness": "firm", "summary": "Always-present selectable filter-model library + Huggett flagship; live click-free hot-swap; stereo, per-Layer.", "children": [
      { "id": "v5.0", "title": "v5.0 — Nonlinear Huggett (plain-tanh spine)", "status": "shipped", "kind": "point", "shipped": "2026-06-20", "summary": "Three asymmetric tanh stages + clean HP pre-filter. Remediated post-UAT (ADAA dropped)." },
      { "id": "v5.1", "title": "v5.1 — Q17 click-free model hot-swap", "status": "planned", "kind": "point", "summary": "Equal-power crossfade on model change; migrate per-voice State from heap to in-place. Prerequisite for any 2nd model." },
      { "id": "v5.2", "title": "v5.2 — Real Separation / dual-filter", "status": "specced", "kind": "point", "summary": "Two independently-tuned 2-pole sections; proper Separation; Summit dual routings; works in 12 and 24 dB.", "specLinks": ["docs/specs/2026-06-20-huggett-separation-design.md"] },
      { "id": "v5.3", "title": "v5.3 — HQ oversampling tiers + on-screen keyboard", "status": "specced", "kind": "point", "summary": "Light/Normal/Heavy/Full OS with independent live/render selectors; playable + MIDI-display keyboard." },
      { "id": "v5.4", "title": "v5.4 — Moog ladder (2nd filter model)", "status": "specced", "kind": "point", "summary": "4-pole self-oscillating; pole-mix BP/HP; bassComp; DC blocker. Gated on hot-swap + OS tiers.", "specLinks": ["docs/specs/2026-06-20-moog-ladder-design.md"] },
      { "id": "v5.5", "title": "v5.5 — Oberheim SEM (3rd filter model)", "status": "planned", "kind": "point", "summary": "2-pole multimode SVF, non-self-oscillating; LP→notch→HP morph + BP." }
    ] },
    { "id": "v6", "title": "v6 — Dynamic VAST Graph Routing", "status": "planned", "kind": "version", "tags": ["keystone"], "firmness": "firm", "summary": "Wired serial+parallel per-voice DSP graph; splits/joins; visual wiring editor." },
    { "id": "v7", "title": "v7 — Source & DSP Block Library", "status": "planned", "kind": "version", "firmness": "firm", "summary": "Osc per-waveform mini-mixer, KVA, noise; DSP blocks (mixer, ring mod, shapers, EQ)." },
    { "id": "v8", "title": "v8 — Ricky: The FX Section", "status": "planned", "kind": "version", "firmness": "firm", "summary": "Post-VCA multi-FX + aux chains; a subset also exposed as VAST blocks." },
    { "id": "v9", "title": "v9 — Multipart, Cascade & Multis", "status": "planned", "kind": "version", "firmness": "firm", "summary": "A/B/Split/Layer surface; toward 32 layers / 256 voices; Multis (≤16)." },
    { "id": "v10", "title": "v10 — FM Layers", "status": "planned", "kind": "version", "firmness": "firm", "summary": "6-operator FM source + operator panel." },
    { "id": "v11", "title": "v11 — Sampling & Keymaps", "status": "planned", "kind": "version", "firmness": "firm", "summary": "Sample/keymap sources (the big v11+ piece, decomposed out)." },
    { "id": "v12", "title": "v12 — Preset System & Patch Management", "status": "tentative", "kind": "version", "firmness": "tentative", "summary": "Browser, tagging, compare/init, patch morph." },
    { "id": "v13", "title": "v13 — Commercialization I", "status": "tentative", "kind": "version", "firmness": "tentative", "summary": "Demo mode + license unlock + macOS port." },
    { "id": "v14", "title": "v14 — Commercial-VST Comparison & RE Rig", "status": "tentative", "kind": "version", "firmness": "tentative", "summary": "Offline-host a commercial VST, capture, compare through the metric catalog." },
    { "id": "v15", "title": "v15 — Filter Model Expansion", "status": "tentative", "kind": "version", "firmness": "tentative", "summary": "Diode ladder + more topologies; gates re-anchored to hardware." },
    { "id": "v16", "title": "v16 — Modulation Deepening", "status": "tentative", "kind": "version", "firmness": "tentative", "summary": "Expanded mod matrix, more LFO/env, advanced voice modes." },
    { "id": "v17", "title": "v17 — Performance Layer", "status": "tentative", "kind": "version", "firmness": "tentative", "summary": "Arpeggiator, sequencer, macros/controllers." },
    { "id": "v18", "title": "v18 — MPE & Per-Note Expression", "status": "tentative", "kind": "version", "firmness": "tentative", "summary": "Per-note pitch/pressure/slide." },
    { "id": "v19", "title": "v19 — Advanced Sources", "status": "tentative", "kind": "version", "firmness": "tentative", "summary": "Wavetable / additive / granular deepening." },
    { "id": "v20", "title": "v20 — The Final Aesthetic", "status": "tentative", "kind": "version", "firmness": "tentative", "summary": "Full target GUI, theming, hi-DPI/scalable UI." },
    { "id": "v21", "title": "v21 — Preset Ecosystem & Factory Library", "status": "tentative", "kind": "version", "firmness": "tentative", "summary": "Packs, sharing, curated factory sounds." },
    { "id": "v22", "title": "v22 — Performance & Scale Hardening", "status": "tentative", "kind": "version", "firmness": "tentative", "summary": "256-voice everywhere, SIMD, multicore." },
    { "id": "v23", "title": "v23 — Platform Expansion", "status": "tentative", "kind": "version", "firmness": "tentative", "summary": "AU/AUv3, CLAP, more hosts/formats." },
    { "id": "v24", "title": "v24 — AI-Assisted Sound Design", "status": "tentative", "kind": "version", "firmness": "tentative", "summary": "Local-LLM patch generation/morphing." },
    { "id": "v25", "title": "v25 — Bernie 1.0: Commercial Flagship Release", "status": "tentative", "kind": "version", "firmness": "tentative", "summary": "Everything converges; 1.0 ship." },
    { "id": "cmajor-spike", "title": "Cmajor Spike — decision gate (pre-v6)", "status": "planned", "kind": "version", "tags": ["gate"], "summary": "Pilot one filter model in Cmajor; verify JUCE integration; prove the 256-voice model. Must resolve before v6 is designed. → ADR." },
    { "id": "cmajor-migration", "title": "Cmajor Migration — conditional (slot TBD by spike)", "status": "tentative", "kind": "version", "tags": ["conditional"], "firmness": "tentative", "summary": "Position decided by the spike. If it wins, v6 is authored in Cmajor (no double-build); else C++ stays." }
  ],
  "continuousThreads": [
    { "id": "thread-harness", "title": "DSP test-harness coverage", "summary": "Grows to cover every spine/source/FX component; gates releases." },
    { "id": "thread-perf", "title": "Per-voice perf gate", "summary": "256 voices × full stereo × graph DSP; profiling as a release gate." },
    { "id": "thread-gui", "title": "Incremental GUI toward the target aesthetic", "summary": "Each phase advances the visual design; no deferred 'real GUI'." },
    { "id": "thread-security", "title": "Security-scan CI baseline", "summary": "SAST + SCA (cppcheck/clang-tidy/CodeQL + dep/CVE scan)." }
  ]
}
```

- [ ] **Step 2: Write the failing store test**

`tools/roadmap-dashboard/server/store.test.ts`:
```ts
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, readFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { loadRoadmap, saveRoadmap, validateDoc } from "./store.ts";
import type { RoadmapDoc } from "../src/types.ts";

function minimalDoc(): RoadmapDoc {
  return {
    meta: { product: "Bernie", tagline: "t", currentVersion: "5", nextStep: "n", lastUpdated: "2026-06-20", schemaVersion: 1 },
    items: [{ id: "v5", title: "v5", status: "in-progress", kind: "version" }],
    continuousThreads: [],
  };
}

test("save then load round-trips", async () => {
  const dir = await mkdtemp(join(tmpdir(), "rm-"));
  const path = join(dir, "roadmap.json");
  const doc = minimalDoc();
  await saveRoadmap(path, doc);
  const loaded = await loadRoadmap(path);
  assert.deepEqual(loaded, doc);
});

test("save writes pretty JSON ending with newline", async () => {
  const dir = await mkdtemp(join(tmpdir(), "rm-"));
  const path = join(dir, "roadmap.json");
  await saveRoadmap(path, minimalDoc());
  const raw = await readFile(path, "utf8");
  assert.ok(raw.endsWith("\n"));
  assert.ok(raw.includes("\n  "));
});

test("validateDoc rejects wrong schema version", () => {
  const bad = { ...minimalDoc(), meta: { ...minimalDoc().meta, schemaVersion: 999 } };
  assert.throws(() => validateDoc(bad), /schemaVersion/);
});

test("validateDoc rejects missing items array", () => {
  assert.throws(() => validateDoc({ meta: minimalDoc().meta, continuousThreads: [] }), /items/);
});

test("validateDoc rejects an item with an unknown status", () => {
  const bad = minimalDoc();
  (bad.items[0] as { status: string }).status = "frobnicated";
  assert.throws(() => validateDoc(bad), /status/);
});

test("validateDoc accepts a valid doc and returns it", () => {
  const doc = minimalDoc();
  assert.deepEqual(validateDoc(doc), doc);
});
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cd tools/roadmap-dashboard && node --test server/store.test.ts`
Expected: FAIL — `Cannot find module './store.ts'`.

- [ ] **Step 4: Implement the store**

`tools/roadmap-dashboard/server/store.ts`:
```ts
import { readFile, writeFile, rename } from "node:fs/promises";
import { SCHEMA_VERSION } from "../src/types.ts";
import type { RoadmapDoc, RoadmapItem, Status, ItemKind } from "../src/types.ts";

const STATUSES: Status[] = ["shipped", "in-progress", "specced", "planned", "tentative", "blocked"];
const KINDS: ItemKind[] = ["version", "point", "feature", "task"];

function isObject(v: unknown): v is Record<string, unknown> {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}

function validateItem(value: unknown, path: string): RoadmapItem {
  if (!isObject(value)) throw new Error(`${path}: item must be an object`);
  if (typeof value.id !== "string" || value.id.length === 0) throw new Error(`${path}: item.id must be a non-empty string`);
  if (typeof value.title !== "string") throw new Error(`${path}: item.title must be a string`);
  if (!STATUSES.includes(value.status as Status)) throw new Error(`${path}: item.status invalid: ${String(value.status)}`);
  if (!KINDS.includes(value.kind as ItemKind)) throw new Error(`${path}: item.kind invalid: ${String(value.kind)}`);
  if (value.children !== undefined) {
    if (!Array.isArray(value.children)) throw new Error(`${path}: item.children must be an array`);
    value.children.forEach((c, i) => validateItem(c, `${path}.children[${i}]`));
  }
  return value as unknown as RoadmapItem;
}

/** Throws Error on any shape/schema violation; returns the doc typed on success. */
export function validateDoc(value: unknown): RoadmapDoc {
  if (!isObject(value)) throw new Error("doc must be an object");
  if (!isObject(value.meta)) throw new Error("doc.meta must be an object");
  if (value.meta.schemaVersion !== SCHEMA_VERSION) {
    throw new Error(`doc.meta.schemaVersion must be ${SCHEMA_VERSION}, got ${String(value.meta.schemaVersion)}`);
  }
  if (!Array.isArray(value.items)) throw new Error("doc.items must be an array");
  value.items.forEach((it, i) => validateItem(it, `items[${i}]`));
  if (!Array.isArray(value.continuousThreads)) throw new Error("doc.continuousThreads must be an array");
  return value as unknown as RoadmapDoc;
}

export async function loadRoadmap(path: string): Promise<RoadmapDoc> {
  const raw = await readFile(path, "utf8");
  return validateDoc(JSON.parse(raw));
}

/** Atomic write: temp file + rename, so a crash can never truncate roadmap.json. */
export async function saveRoadmap(path: string, doc: RoadmapDoc): Promise<void> {
  validateDoc(doc);
  const tmp = `${path}.tmp-${process.pid}`;
  await writeFile(tmp, JSON.stringify(doc, null, 2) + "\n", "utf8");
  await rename(tmp, path);
}
```

- [ ] **Step 5: Run the store test to verify it passes**

Run: `cd tools/roadmap-dashboard && node --test server/store.test.ts`
Expected: PASS — 6 tests pass.

- [ ] **Step 6: Verify the seed file loads cleanly**

Run: `cd tools/roadmap-dashboard && node --input-type=module -e "import('./server/store.ts').then(m => m.loadRoadmap('./roadmap.json')).then(d => console.log('OK items=' + d.items.length)).catch(e => { console.error(e.message); process.exit(1); })"`
Expected: prints `OK items=28` (v1–v4.5 = 5, v5–v25 = 21, + 2 Cmajor entries = 28 top-level items). If it prints a validation error, fix the seed JSON.

- [ ] **Step 7: Commit**

```bash
git add tools/roadmap-dashboard/roadmap.json tools/roadmap-dashboard/server/store.ts \
        tools/roadmap-dashboard/server/store.test.ts
git commit -m "feat(roadmap): seed roadmap.json + atomic store with schema validation"
```

---

### Task 3: HTTP server (GET/PUT roadmap + static serving)

**Files:**
- Create: `tools/roadmap-dashboard/server/server.ts`
- Create: `tools/roadmap-dashboard/index.html`
- Test: `tools/roadmap-dashboard/server/server.test.ts`

**Interfaces:**
- Consumes: `loadRoadmap`, `saveRoadmap`, `validateDoc` from `./store.ts`.
- Produces: `createServer(opts: { roadmapPath: string; rootDir: string }): http.Server` (exported for tests, not auto-listening); the module, when run directly, listens on `PORT` (default 4173). Routes: `GET /api/roadmap`, `PUT /api/roadmap`, `POST /api/decompose` (added in Task 7), `GET /*` static.

- [ ] **Step 1: Create the HTML shell**

`tools/roadmap-dashboard/index.html`:
```html
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Bernie — Roadmap</title>
  <link rel="stylesheet" href="/src/styles.css" />
</head>
<body>
  <div id="app">Loading roadmap…</div>
  <script src="/dist/bundle.js"></script>
</body>
</html>
```

- [ ] **Step 2: Write the failing server test**

`tools/roadmap-dashboard/server/server.test.ts`:
```ts
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import type { AddressInfo } from "node:net";
import { createServer } from "./server.ts";
import type { RoadmapDoc } from "../src/types.ts";

const here = dirname(fileURLToPath(import.meta.url));
const rootDir = join(here, "..");

function doc(): RoadmapDoc {
  return {
    meta: { product: "Bernie", tagline: "t", currentVersion: "5", nextStep: "n", lastUpdated: "2026-06-20", schemaVersion: 1 },
    items: [{ id: "v5", title: "v5", status: "in-progress", kind: "version" }],
    continuousThreads: [],
  };
}

let base = "";
let server: ReturnType<typeof createServer>;
let roadmapPath = "";

before(async () => {
  const dir = await mkdtemp(join(tmpdir(), "rm-srv-"));
  roadmapPath = join(dir, "roadmap.json");
  await writeFile(roadmapPath, JSON.stringify(doc(), null, 2));
  server = createServer({ roadmapPath, rootDir });
  await new Promise<void>((res) => server.listen(0, res));
  const port = (server.address() as AddressInfo).port;
  base = `http://127.0.0.1:${port}`;
});

after(() => server.close());

test("GET /api/roadmap returns the document", async () => {
  const r = await fetch(`${base}/api/roadmap`);
  assert.equal(r.status, 200);
  const body = await r.json() as RoadmapDoc;
  assert.equal(body.items[0].id, "v5");
});

test("PUT /api/roadmap persists a valid document", async () => {
  const next = doc();
  next.meta.nextStep = "changed";
  const put = await fetch(`${base}/api/roadmap`, {
    method: "PUT", headers: { "content-type": "application/json" }, body: JSON.stringify(next),
  });
  assert.equal(put.status, 200);
  const get = await fetch(`${base}/api/roadmap`);
  const body = await get.json() as RoadmapDoc;
  assert.equal(body.meta.nextStep, "changed");
});

test("PUT of an invalid document is rejected and leaves the file unchanged", async () => {
  const bad = { meta: { schemaVersion: 1 }, items: "nope", continuousThreads: [] };
  const put = await fetch(`${base}/api/roadmap`, {
    method: "PUT", headers: { "content-type": "application/json" }, body: JSON.stringify(bad),
  });
  assert.equal(put.status, 400);
  const get = await fetch(`${base}/api/roadmap`);
  const body = await get.json() as RoadmapDoc;
  assert.equal(body.items[0].id, "v5"); // unchanged from prior test's valid PUT
});

test("GET / serves the HTML shell", async () => {
  const r = await fetch(`${base}/`);
  assert.equal(r.status, 200);
  assert.match(r.headers.get("content-type") ?? "", /text\/html/);
  const text = await r.text();
  assert.match(text, /id="app"/);
});
```

- [ ] **Step 3: Run the server test to verify it fails**

Run: `cd tools/roadmap-dashboard && node --test server/server.test.ts`
Expected: FAIL — `Cannot find module './server.ts'`.

- [ ] **Step 4: Implement the server**

`tools/roadmap-dashboard/server/server.ts`:
```ts
import http from "node:http";
import { readFile } from "node:fs/promises";
import { join, normalize, extname } from "node:path";
import { loadRoadmap, saveRoadmap, validateDoc } from "./store.ts";

interface Options { roadmapPath: string; rootDir: string; }

const MIME: Record<string, string> = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
};

function send(res: http.ServerResponse, status: number, type: string, body: string): void {
  res.writeHead(status, { "content-type": type });
  res.end(body);
}

async function readBody(req: http.IncomingMessage): Promise<string> {
  const chunks: Buffer[] = [];
  for await (const c of req) chunks.push(c as Buffer);
  return Buffer.concat(chunks).toString("utf8");
}

export function createServer(opts: Options): http.Server {
  return http.createServer(async (req, res) => {
    try {
      const url = new URL(req.url ?? "/", "http://localhost");
      const path = url.pathname;

      if (path === "/api/roadmap" && req.method === "GET") {
        const doc = await loadRoadmap(opts.roadmapPath);
        return send(res, 200, MIME[".json"], JSON.stringify(doc));
      }

      if (path === "/api/roadmap" && req.method === "PUT") {
        const raw = await readBody(req);
        let doc;
        try { doc = validateDoc(JSON.parse(raw)); }
        catch (e) { return send(res, 400, MIME[".json"], JSON.stringify({ error: (e as Error).message })); }
        await saveRoadmap(opts.roadmapPath, doc);
        return send(res, 200, MIME[".json"], JSON.stringify({ ok: true }));
      }

      // Static files (index.html for "/", otherwise the requested path under rootDir).
      const rel = path === "/" ? "index.html" : path.replace(/^\/+/, "");
      const safe = normalize(rel).replace(/^(\.\.[/\\])+/, "");
      const filePath = join(opts.rootDir, safe);
      if (!filePath.startsWith(opts.rootDir)) return send(res, 403, MIME[".html"], "Forbidden");
      try {
        const data = await readFile(filePath);
        const type = MIME[extname(filePath)] ?? "application/octet-stream";
        res.writeHead(200, { "content-type": type });
        return res.end(data);
      } catch {
        return send(res, 404, MIME[".html"], "Not found");
      }
    } catch (e) {
      return send(res, 500, MIME[".json"], JSON.stringify({ error: (e as Error).message }));
    }
  });
}

// Auto-listen when run directly (node server/server.ts).
const isMain = process.argv[1] && import.meta.url === `file://${process.argv[1]}`;
if (isMain) {
  const rootDir = join(import.meta.dirname, "..");
  const roadmapPath = join(rootDir, "roadmap.json");
  const port = Number(process.env.PORT ?? 4173);
  createServer({ roadmapPath, rootDir }).listen(port, () => {
    console.log(`Bernie roadmap dashboard → http://localhost:${port}`);
  });
}
```

- [ ] **Step 5: Run the server test to verify it passes**

Run: `cd tools/roadmap-dashboard && node --test server/server.test.ts`
Expected: PASS — 4 tests pass.

- [ ] **Step 6: Commit**

```bash
git add tools/roadmap-dashboard/server/server.ts tools/roadmap-dashboard/index.html \
        tools/roadmap-dashboard/server/server.test.ts
git commit -m "feat(roadmap): http server with GET/PUT roadmap + static serving"
```

---

### Task 4: UI render (tree → HTML) + styles + bootstrap

**Files:**
- Create: `tools/roadmap-dashboard/src/render.ts`
- Create: `tools/roadmap-dashboard/src/api.ts`
- Create: `tools/roadmap-dashboard/src/main.ts`
- Create: `tools/roadmap-dashboard/src/styles.css`
- Test: `tools/roadmap-dashboard/src/render.test.ts`

**Interfaces:**
- Consumes: `RoadmapDoc`, `RoadmapItem` from `./types.ts`; `computeProgress` from `./progress.ts`.
- Produces: `renderDoc(doc: RoadmapDoc): string`; `renderItem(item: RoadmapItem): string`; `escapeHtml(s: string): string`. `api.ts` produces `getRoadmap(): Promise<RoadmapDoc>` and `putRoadmap(doc: RoadmapDoc): Promise<void>`.

- [ ] **Step 1: Write the failing render test**

`tools/roadmap-dashboard/src/render.test.ts`:
```ts
import { test } from "node:test";
import assert from "node:assert/strict";
import { renderDoc, renderItem, escapeHtml } from "./render.ts";
import type { RoadmapDoc, RoadmapItem } from "./types.ts";

test("escapeHtml neutralizes angle brackets and ampersands", () => {
  assert.equal(escapeHtml(`a<b>&"c`), `a&lt;b&gt;&amp;&quot;c`);
});

test("renderItem includes title, status badge, and a data-id", () => {
  const item: RoadmapItem = { id: "v5", title: "v5 — Spine", status: "in-progress", kind: "version" };
  const html = renderItem(item);
  assert.match(html, /data-id="v5"/);
  assert.match(html, /v5 — Spine/);
  assert.match(html, /in-progress/);
});

test("renderItem shows a progress bar with the rolled-up percent", () => {
  const item: RoadmapItem = {
    id: "v5", title: "v5", status: "in-progress", kind: "version",
    children: [
      { id: "a", title: "a", status: "shipped", kind: "point" },
      { id: "b", title: "b", status: "planned", kind: "point" },
    ],
  };
  const html = renderItem(item);
  assert.match(html, /width:\s*50%/);
});

test("renderItem recurses into children", () => {
  const item: RoadmapItem = {
    id: "v5", title: "v5", status: "in-progress", kind: "version",
    children: [{ id: "v5.1", title: "hot-swap", status: "planned", kind: "point" }],
  };
  const html = renderItem(item);
  assert.match(html, /data-id="v5.1"/);
  assert.match(html, /hot-swap/);
});

test("renderDoc renders meta header and every top-level item", () => {
  const doc: RoadmapDoc = {
    meta: { product: "Bernie", tagline: "tag", currentVersion: "5", nextStep: "next!", lastUpdated: "2026-06-20", schemaVersion: 1 },
    items: [
      { id: "v5", title: "v5", status: "in-progress", kind: "version" },
      { id: "v6", title: "v6", status: "planned", kind: "version" },
    ],
    continuousThreads: [{ id: "t1", title: "harness" }],
  };
  const html = renderDoc(doc);
  assert.match(html, /Bernie/);
  assert.match(html, /next!/);
  assert.match(html, /data-id="v5"/);
  assert.match(html, /data-id="v6"/);
  assert.match(html, /harness/);
});
```

- [ ] **Step 2: Run the render test to verify it fails**

Run: `cd tools/roadmap-dashboard && node --test src/render.test.ts`
Expected: FAIL — `Cannot find module './render.ts'`.

- [ ] **Step 3: Implement the renderer**

`tools/roadmap-dashboard/src/render.ts`:
```ts
import type { RoadmapDoc, RoadmapItem } from "./types.ts";
import { computeProgress } from "./progress.ts";

export function escapeHtml(s: string): string {
  return s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function tagBadges(item: RoadmapItem): string {
  if (!item.tags || item.tags.length === 0) return "";
  return item.tags.map((t) => `<span class="tag tag-${t}">${escapeHtml(t)}</span>`).join("");
}

function specLinks(item: RoadmapItem): string {
  if (!item.specLinks || item.specLinks.length === 0) return "";
  return `<div class="spec-links">` +
    item.specLinks.map((l) => `<a href="#" data-spec="${escapeHtml(l)}">${escapeHtml(l.split("/").pop() ?? l)}</a>`).join(" ") +
    `</div>`;
}

export function renderItem(item: RoadmapItem): string {
  const pct = computeProgress(item);
  const summary = item.summary ? `<div class="summary">${escapeHtml(item.summary)}</div>` : "";
  const children = item.children && item.children.length > 0
    ? `<div class="children">${item.children.map(renderItem).join("")}</div>`
    : "";
  return `
    <div class="item kind-${item.kind} status-${item.status}" draggable="true" data-id="${escapeHtml(item.id)}">
      <div class="item-head">
        <span class="drag-handle" title="Drag to reorder">⋮⋮</span>
        <span class="title" data-action="edit">${escapeHtml(item.title)}</span>
        <span class="status-badge">${escapeHtml(item.status)}</span>
        ${tagBadges(item)}
        <span class="spacer"></span>
        <button data-action="add-child" title="Add sub-item">＋</button>
        <button data-action="decompose" title="Decompose with Claude">⚡</button>
        <button data-action="delete" title="Delete">✕</button>
      </div>
      <div class="progress" title="${pct}%"><div class="bar" style="width: ${pct}%"></div></div>
      ${summary}
      ${specLinks(item)}
      ${children}
    </div>`;
}

export function renderDoc(doc: RoadmapDoc): string {
  const m = doc.meta;
  const threads = doc.continuousThreads
    .map((t) => `<li title="${escapeHtml(t.summary ?? "")}">${escapeHtml(t.title)}</li>`).join("");
  const items = doc.items.map(renderItem).join("");
  return `
    <header class="topbar">
      <div class="brand"><h1>${escapeHtml(m.product)}</h1><p>${escapeHtml(m.tagline)}</p></div>
      <div class="now">
        <span class="pill">You are here: v${escapeHtml(m.currentVersion)}</span>
        <span class="next">Next: ${escapeHtml(m.nextStep)}</span>
        <span class="updated">Updated ${escapeHtml(m.lastUpdated)}</span>
      </div>
    </header>
    <section class="filters" data-role="filters"></section>
    <main class="timeline">${items}
      <button class="add-version" data-action="add-version">＋ Add version</button>
    </main>
    <aside class="threads"><h2>Continuous threads</h2><ul>${threads}</ul></aside>`;
}
```

- [ ] **Step 4: Run the render test to verify it passes**

Run: `cd tools/roadmap-dashboard && node --test src/render.test.ts`
Expected: PASS — 5 tests pass.

- [ ] **Step 5: Implement the API client**

`tools/roadmap-dashboard/src/api.ts`:
```ts
import type { RoadmapDoc } from "./types.ts";

export async function getRoadmap(): Promise<RoadmapDoc> {
  const r = await fetch("/api/roadmap");
  if (!r.ok) throw new Error(`GET /api/roadmap failed: ${r.status}`);
  return r.json() as Promise<RoadmapDoc>;
}

export async function putRoadmap(doc: RoadmapDoc): Promise<void> {
  const r = await fetch("/api/roadmap", {
    method: "PUT",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(doc),
  });
  if (!r.ok) {
    const msg = await r.text();
    throw new Error(`PUT /api/roadmap failed: ${r.status} ${msg}`);
  }
}
```

- [ ] **Step 6: Implement the bootstrap (render-only for now)**

`tools/roadmap-dashboard/src/main.ts`:
```ts
import { getRoadmap } from "./api.ts";
import { renderDoc } from "./render.ts";
import type { RoadmapDoc } from "./types.ts";

let doc: RoadmapDoc;

async function refresh(): Promise<void> {
  doc = await getRoadmap();
  const app = document.getElementById("app");
  if (app) app.innerHTML = renderDoc(doc);
}

refresh().catch((e) => {
  const app = document.getElementById("app");
  if (app) app.textContent = `Failed to load roadmap: ${(e as Error).message}`;
});
```

- [ ] **Step 7: Implement the stylesheet**

`tools/roadmap-dashboard/src/styles.css` — compact dark Summit-panel aesthetic:
```css
:root {
  --bg: #14171c; --panel: #1c2128; --panel-2: #232a33; --ink: #e6edf3;
  --muted: #8b97a7; --accent: #f2a900; --line: #30363d;
  --shipped: #3fb950; --inprogress: #58a6ff; --specced: #a371f7;
  --planned: #8b97a7; --tentative: #6e7681; --blocked: #f85149;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink);
  font: 13px/1.45 ui-sans-serif, system-ui, -apple-system, Segoe UI, Roboto, sans-serif; }
#app { display: grid; grid-template-columns: 1fr 240px; grid-template-rows: auto auto 1fr;
  grid-template-areas: "top top" "filters filters" "timeline threads"; gap: 12px; padding: 14px; min-height: 100vh; }
.topbar { grid-area: top; display: flex; justify-content: space-between; align-items: center;
  background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 12px 16px; }
.brand h1 { margin: 0; font-size: 18px; letter-spacing: .5px; }
.brand p { margin: 2px 0 0; color: var(--muted); }
.now { text-align: right; display: flex; flex-direction: column; gap: 3px; }
.pill { background: var(--accent); color: #1a1300; font-weight: 600; border-radius: 999px; padding: 2px 10px; }
.next { color: var(--inprogress); } .updated { color: var(--muted); font-size: 11px; }
.filters { grid-area: filters; display: flex; gap: 6px; flex-wrap: wrap; }
.timeline { grid-area: timeline; display: flex; flex-direction: column; gap: 8px; }
.threads { grid-area: threads; background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 10px 12px; height: fit-content; }
.threads h2 { font-size: 12px; text-transform: uppercase; color: var(--muted); margin: 0 0 6px; }
.threads ul { margin: 0; padding-left: 16px; } .threads li { margin: 4px 0; }
.item { background: var(--panel); border: 1px solid var(--line); border-left: 3px solid var(--planned);
  border-radius: 8px; padding: 8px 10px; }
.item.kind-point, .item.kind-feature, .item.kind-task { background: var(--panel-2); }
.item.status-shipped { border-left-color: var(--shipped); }
.item.status-in-progress { border-left-color: var(--inprogress); }
.item.status-specced { border-left-color: var(--specced); }
.item.status-tentative { border-left-color: var(--tentative); opacity: .82; }
.item.status-blocked { border-left-color: var(--blocked); }
.item-head { display: flex; align-items: center; gap: 8px; }
.drag-handle { cursor: grab; color: var(--muted); }
.title { font-weight: 600; cursor: text; } .spacer { flex: 1; }
.status-badge { font-size: 10px; text-transform: uppercase; color: var(--muted); border: 1px solid var(--line); border-radius: 4px; padding: 1px 5px; }
.tag { font-size: 10px; border-radius: 4px; padding: 1px 5px; margin-left: 2px; }
.tag-keystone { background: #3a2d00; color: var(--accent); }
.tag-gate { background: #2d1a3a; color: var(--specced); }
.tag-conditional { background: #1a2d3a; color: var(--inprogress); }
.item-head button { background: transparent; border: none; color: var(--muted); cursor: pointer; font-size: 13px; padding: 2px 4px; border-radius: 4px; }
.item-head button:hover { background: var(--line); color: var(--ink); }
.progress { height: 4px; background: var(--line); border-radius: 3px; margin: 6px 0; overflow: hidden; }
.progress .bar { height: 100%; background: var(--accent); }
.summary { color: var(--muted); font-size: 12px; }
.spec-links { margin-top: 4px; } .spec-links a { color: var(--specced); font-size: 11px; margin-right: 8px; }
.children { margin: 8px 0 0 18px; display: flex; flex-direction: column; gap: 6px; border-left: 1px dashed var(--line); padding-left: 10px; }
.add-version, .filters button { background: var(--panel-2); color: var(--ink); border: 1px solid var(--line); border-radius: 6px; padding: 4px 10px; cursor: pointer; }
.filters button.active { background: var(--accent); color: #1a1300; }
.item.drag-over { outline: 2px dashed var(--accent); }
```

- [ ] **Step 8: Build and smoke-test the UI manually**

Run: `cd tools/roadmap-dashboard && npm run dashboard`
Then open `http://localhost:4173`. Expected: the header shows "Bernie", "You are here: v5", "Next: v5.1 …"; the timeline lists v1…v25 plus the Cmajor entries; v5 expands to its point releases with progress bars; the continuous-threads sidebar lists four threads. Stop the server (Ctrl-C) when verified.

- [ ] **Step 9: Commit**

```bash
git add tools/roadmap-dashboard/src/render.ts tools/roadmap-dashboard/src/render.test.ts \
        tools/roadmap-dashboard/src/api.ts tools/roadmap-dashboard/src/main.ts \
        tools/roadmap-dashboard/src/styles.css
git commit -m "feat(roadmap): render tree to HTML + API client + dark Summit styles"
```

---

### Task 5: Inline edit — add / edit / delete (pure transforms wired to the DOM)

**Files:**
- Create: `tools/roadmap-dashboard/src/edit.ts`
- Test: `tools/roadmap-dashboard/src/edit.test.ts`
- Modify: `tools/roadmap-dashboard/src/main.ts`

**Interfaces:**
- Consumes: `RoadmapDoc`, `RoadmapItem`, `Status`, `ItemKind` from `./types.ts`.
- Produces (pure, immutable transforms on `doc.items`): `findItem(doc, id): RoadmapItem | undefined`; `updateItem(doc, id, patch: Partial<RoadmapItem>): RoadmapDoc`; `deleteItem(doc, id): RoadmapDoc`; `addChild(doc, parentId, child: RoadmapItem): RoadmapDoc`; `addVersion(doc, item: RoadmapItem): RoadmapDoc`; `newItem(kind: ItemKind, title: string): RoadmapItem` (generates a unique id).

- [ ] **Step 1: Write the failing edit test**

`tools/roadmap-dashboard/src/edit.test.ts`:
```ts
import { test } from "node:test";
import assert from "node:assert/strict";
import { findItem, updateItem, deleteItem, addChild, addVersion, newItem } from "./edit.ts";
import type { RoadmapDoc } from "./types.ts";

function doc(): RoadmapDoc {
  return {
    meta: { product: "Bernie", tagline: "t", currentVersion: "5", nextStep: "n", lastUpdated: "2026-06-20", schemaVersion: 1 },
    items: [
      { id: "v5", title: "v5", status: "in-progress", kind: "version",
        children: [{ id: "v5.1", title: "hot-swap", status: "planned", kind: "point" }] },
      { id: "v6", title: "v6", status: "planned", kind: "version" },
    ],
    continuousThreads: [],
  };
}

test("findItem locates nested items", () => {
  assert.equal(findItem(doc(), "v5.1")?.title, "hot-swap");
  assert.equal(findItem(doc(), "nope"), undefined);
});

test("updateItem patches a nested item without mutating the input", () => {
  const before = doc();
  const after = updateItem(before, "v5.1", { status: "in-progress", title: "Hot-swap!" });
  assert.equal(findItem(after, "v5.1")?.status, "in-progress");
  assert.equal(findItem(after, "v5.1")?.title, "Hot-swap!");
  assert.equal(findItem(before, "v5.1")?.status, "planned"); // input untouched
});

test("deleteItem removes a nested item", () => {
  const after = deleteItem(doc(), "v5.1");
  assert.equal(findItem(after, "v5.1"), undefined);
  assert.equal(findItem(after, "v5")?.children?.length ?? 0, 0);
});

test("deleteItem removes a top-level item", () => {
  const after = deleteItem(doc(), "v6");
  assert.equal(after.items.length, 1);
});

test("addChild appends under the named parent", () => {
  const child = newItem("task", "do the thing");
  const after = addChild(doc(), "v5", child);
  const kids = findItem(after, "v5")?.children ?? [];
  assert.equal(kids.length, 2);
  assert.equal(kids[1].title, "do the thing");
});

test("addVersion appends at the top level", () => {
  const after = addVersion(doc(), newItem("version", "v26"));
  assert.equal(after.items.length, 3);
  assert.equal(after.items[2].title, "v26");
});

test("newItem generates unique ids and sane defaults", () => {
  const a = newItem("task", "x");
  const b = newItem("task", "x");
  assert.notEqual(a.id, b.id);
  assert.equal(a.kind, "task");
  assert.equal(a.status, "planned");
});
```

- [ ] **Step 2: Run the edit test to verify it fails**

Run: `cd tools/roadmap-dashboard && node --test src/edit.test.ts`
Expected: FAIL — `Cannot find module './edit.ts'`.

- [ ] **Step 3: Implement the edit transforms**

`tools/roadmap-dashboard/src/edit.ts`:
```ts
import type { RoadmapDoc, RoadmapItem, ItemKind } from "./types.ts";

export function findItem(doc: RoadmapDoc, id: string): RoadmapItem | undefined {
  const walk = (items: RoadmapItem[]): RoadmapItem | undefined => {
    for (const it of items) {
      if (it.id === id) return it;
      if (it.children) { const found = walk(it.children); if (found) return found; }
    }
    return undefined;
  };
  return walk(doc.items);
}

function mapItems(items: RoadmapItem[], fn: (it: RoadmapItem) => RoadmapItem): RoadmapItem[] {
  return items.map((it) => {
    const mapped = fn(it);
    if (mapped.children) return { ...mapped, children: mapItems(mapped.children, fn) };
    return mapped;
  });
}

export function updateItem(doc: RoadmapDoc, id: string, patch: Partial<RoadmapItem>): RoadmapDoc {
  return { ...doc, items: mapItems(doc.items, (it) => (it.id === id ? { ...it, ...patch } : it)) };
}

function removeItems(items: RoadmapItem[], id: string): RoadmapItem[] {
  return items
    .filter((it) => it.id !== id)
    .map((it) => (it.children ? { ...it, children: removeItems(it.children, id) } : it));
}

export function deleteItem(doc: RoadmapDoc, id: string): RoadmapDoc {
  return { ...doc, items: removeItems(doc.items, id) };
}

export function addChild(doc: RoadmapDoc, parentId: string, child: RoadmapItem): RoadmapDoc {
  return {
    ...doc,
    items: mapItems(doc.items, (it) =>
      it.id === parentId ? { ...it, children: [...(it.children ?? []), child] } : it),
  };
}

export function addVersion(doc: RoadmapDoc, item: RoadmapItem): RoadmapDoc {
  return { ...doc, items: [...doc.items, item] };
}

let counter = 0;
export function newItem(kind: ItemKind, title: string): RoadmapItem {
  counter += 1;
  const id = `${kind}-${Date.now().toString(36)}-${counter}`;
  return { id, title, status: "planned", kind };
}
```

- [ ] **Step 4: Run the edit test to verify it passes**

Run: `cd tools/roadmap-dashboard && node --test src/edit.test.ts`
Expected: PASS — 7 tests pass.

- [ ] **Step 5: Wire edit actions into the bootstrap**

Replace the body of `tools/roadmap-dashboard/src/main.ts` with:
```ts
import { getRoadmap, putRoadmap } from "./api.ts";
import { renderDoc } from "./render.ts";
import { findItem, updateItem, deleteItem, addChild, addVersion, newItem } from "./edit.ts";
import type { RoadmapDoc, RoadmapItem, Status, ItemKind } from "./types.ts";

let doc: RoadmapDoc;
const app = () => document.getElementById("app") as HTMLElement;

function paint(): void { app().innerHTML = renderDoc(doc); }

async function commit(next: RoadmapDoc): Promise<void> {
  doc = next;
  paint();
  await putRoadmap(doc);
}

const STATUS_CYCLE: Status[] = ["planned", "specced", "in-progress", "shipped", "blocked", "tentative"];

function itemEl(target: HTMLElement): HTMLElement | null {
  return target.closest<HTMLElement>(".item");
}

async function onClick(ev: MouseEvent): Promise<void> {
  const t = ev.target as HTMLElement;
  const action = t.dataset.action;
  if (!action) return;
  const el = itemEl(t);
  const id = el?.dataset.id;

  if (action === "add-version") {
    const title = prompt("New version title?", "v26 — ");
    if (title) await commit(addVersion(doc, newItem("version", title)));
    return;
  }
  if (!id) return;
  const item = findItem(doc, id);
  if (!item) return;

  if (action === "edit") {
    const title = prompt("Title:", item.title);
    if (title !== null && title !== item.title) { await commit(updateItem(doc, id, { title })); return; }
    // If the title is unchanged, offer a status cycle instead.
    const cur = STATUS_CYCLE.indexOf(item.status);
    const next = STATUS_CYCLE[(cur + 1) % STATUS_CYCLE.length];
    await commit(updateItem(doc, id, { status: next }));
  } else if (action === "delete") {
    if (confirm(`Delete "${item.title}" and its sub-items?`)) await commit(deleteItem(doc, id));
  } else if (action === "add-child") {
    const kind: ItemKind = item.kind === "version" ? "point" : item.kind === "point" ? "feature" : "task";
    const title = prompt(`New ${kind} under "${item.title}":`, "");
    if (title) await commit(addChild(doc, id, newItem(kind, title)));
  } else if (action === "decompose") {
    await onDecompose(id); // implemented in Task 7
  }
}

async function refresh(): Promise<void> {
  doc = await getRoadmap();
  paint();
}

app().addEventListener("click", (ev) => { void onClick(ev as MouseEvent); });

refresh().catch((e) => { app().textContent = `Failed to load roadmap: ${(e as Error).message}`; });

// Placeholder until Task 7 implements the handoff.
async function onDecompose(_id: string): Promise<void> { alert("Decompose lands in Task 7."); }

export { commit, doc };
```

- [ ] **Step 6: Build to confirm the bundle compiles**

Run: `cd tools/roadmap-dashboard && npm run build`
Expected: esbuild writes `dist/bundle.js` with no errors.

- [ ] **Step 7: Manual smoke — edit, add, delete**

Run `npm run dashboard`, open the page. Click a title → rename it; click again with no change → status cycles; ＋ adds a sub-item; ✕ deletes. Reload the page and confirm changes persisted (they were written to `roadmap.json`). Then `git checkout tools/roadmap-dashboard/roadmap.json` to discard smoke edits.

- [ ] **Step 8: Commit**

```bash
git add tools/roadmap-dashboard/src/edit.ts tools/roadmap-dashboard/src/edit.test.ts \
        tools/roadmap-dashboard/src/main.ts
git commit -m "feat(roadmap): inline add/edit/delete with immutable transforms"
```

---

### Task 6: Drag-to-reorder + re-parent (pure move transform wired to DOM drag events)

**Files:**
- Create: `tools/roadmap-dashboard/src/reorder.ts`
- Test: `tools/roadmap-dashboard/src/reorder.test.ts`
- Modify: `tools/roadmap-dashboard/src/main.ts`

**Interfaces:**
- Consumes: `RoadmapDoc`, `RoadmapItem` from `./types.ts`.
- Produces: `moveItem(doc, draggedId, targetId, position: "before" | "after" | "inside"): RoadmapDoc`. A no-op (returns input unchanged) if the move is invalid (dragging onto self or into own descendant, or unknown ids).

- [ ] **Step 1: Write the failing reorder test**

`tools/roadmap-dashboard/src/reorder.test.ts`:
```ts
import { test } from "node:test";
import assert from "node:assert/strict";
import { moveItem } from "./reorder.ts";
import { findItem } from "./edit.ts";
import type { RoadmapDoc } from "./types.ts";

function doc(): RoadmapDoc {
  return {
    meta: { product: "Bernie", tagline: "t", currentVersion: "5", nextStep: "n", lastUpdated: "2026-06-20", schemaVersion: 1 },
    items: [
      { id: "v5", title: "v5", status: "in-progress", kind: "version",
        children: [
          { id: "v5.1", title: "a", status: "planned", kind: "point" },
          { id: "v5.2", title: "b", status: "planned", kind: "point" },
        ] },
      { id: "v6", title: "v6", status: "planned", kind: "version" },
      { id: "v7", title: "v7", status: "planned", kind: "version" },
    ],
    continuousThreads: [],
  };
}

test("move a top-level item before another reorders the list", () => {
  const after = moveItem(doc(), "v7", "v6", "before");
  assert.deepEqual(after.items.map((i) => i.id), ["v5", "v7", "v6"]);
});

test("move a top-level item after another", () => {
  const after = moveItem(doc(), "v5", "v6", "after");
  assert.deepEqual(after.items.map((i) => i.id), ["v6", "v5", "v7"]);
});

test("reorder children within a parent", () => {
  const after = moveItem(doc(), "v5.2", "v5.1", "before");
  assert.deepEqual(findItem(after, "v5")?.children?.map((c) => c.id), ["v5.2", "v5.1"]);
});

test("re-parent a child onto a top-level item with 'inside'", () => {
  const after = moveItem(doc(), "v5.1", "v6", "inside");
  assert.equal(findItem(after, "v5")?.children?.length, 1);
  assert.deepEqual(findItem(after, "v6")?.children?.map((c) => c.id), ["v5.1"]);
});

test("dropping an item onto itself is a no-op", () => {
  const before = doc();
  const after = moveItem(before, "v6", "v6", "before");
  assert.deepEqual(after.items.map((i) => i.id), before.items.map((i) => i.id));
});

test("dropping a parent into its own descendant is a no-op", () => {
  const before = doc();
  const after = moveItem(before, "v5", "v5.1", "inside");
  assert.deepEqual(after, before);
});

test("unknown ids are a no-op", () => {
  const before = doc();
  assert.deepEqual(moveItem(before, "nope", "v6", "before"), before);
});
```

- [ ] **Step 2: Run the reorder test to verify it fails**

Run: `cd tools/roadmap-dashboard && node --test src/reorder.test.ts`
Expected: FAIL — `Cannot find module './reorder.ts'`.

- [ ] **Step 3: Implement the move transform**

`tools/roadmap-dashboard/src/reorder.ts`:
```ts
import type { RoadmapDoc, RoadmapItem } from "./types.ts";

function clone(items: RoadmapItem[]): RoadmapItem[] {
  return items.map((it) => ({ ...it, children: it.children ? clone(it.children) : undefined }));
}

function isDescendant(node: RoadmapItem, id: string): boolean {
  if (node.id === id) return true;
  return (node.children ?? []).some((c) => isDescendant(c, id));
}

/** Remove the item with `id` from the tree, returning it (or null). Mutates `items`. */
function extract(items: RoadmapItem[], id: string): RoadmapItem | null {
  const idx = items.findIndex((it) => it.id === id);
  if (idx >= 0) { const [removed] = items.splice(idx, 1); return removed; }
  for (const it of items) {
    if (it.children) { const r = extract(it.children, id); if (r) return r; }
  }
  return null;
}

function insertRelative(items: RoadmapItem[], targetId: string, node: RoadmapItem, before: boolean): boolean {
  const idx = items.findIndex((it) => it.id === targetId);
  if (idx >= 0) { items.splice(before ? idx : idx + 1, 0, node); return true; }
  for (const it of items) {
    if (it.children && insertRelative(it.children, targetId, node, before)) return true;
  }
  return false;
}

function insertInside(items: RoadmapItem[], targetId: string, node: RoadmapItem): boolean {
  for (const it of items) {
    if (it.id === targetId) { it.children = [...(it.children ?? []), node]; return true; }
    if (it.children && insertInside(it.children, targetId, node)) return true;
  }
  return false;
}

export function moveItem(
  doc: RoadmapDoc,
  draggedId: string,
  targetId: string,
  position: "before" | "after" | "inside",
): RoadmapDoc {
  if (draggedId === targetId) return doc;
  const dragged = findRaw(doc.items, draggedId);
  if (!dragged || !findRaw(doc.items, targetId)) return doc;
  if (isDescendant(dragged, targetId)) return doc; // can't drop into own subtree

  const items = clone(doc.items);
  const node = extract(items, draggedId);
  if (!node) return doc;

  const ok = position === "inside"
    ? insertInside(items, targetId, node)
    : insertRelative(items, targetId, node, position === "before");
  if (!ok) return doc; // target vanished (shouldn't happen) — bail without losing data
  return { ...doc, items };
}

function findRaw(items: RoadmapItem[], id: string): RoadmapItem | undefined {
  for (const it of items) {
    if (it.id === id) return it;
    if (it.children) { const f = findRaw(it.children, id); if (f) return f; }
  }
  return undefined;
}
```

- [ ] **Step 4: Run the reorder test to verify it passes**

Run: `cd tools/roadmap-dashboard && node --test src/reorder.test.ts`
Expected: PASS — 7 tests pass.

- [ ] **Step 5: Wire HTML5 drag-and-drop into the bootstrap**

Add to `tools/roadmap-dashboard/src/main.ts` — import `moveItem` and register drag handlers. Add the import at the top:
```ts
import { moveItem } from "./reorder.ts";
```
Then append this block before the final `export`:
```ts
let draggedId: string | null = null;

app().addEventListener("dragstart", (ev) => {
  const el = (ev.target as HTMLElement).closest<HTMLElement>(".item");
  if (el) { draggedId = el.dataset.id ?? null; ev.dataTransfer?.setData("text/plain", draggedId ?? ""); }
});

app().addEventListener("dragover", (ev) => {
  const el = (ev.target as HTMLElement).closest<HTMLElement>(".item");
  if (el && draggedId && el.dataset.id !== draggedId) { ev.preventDefault(); el.classList.add("drag-over"); }
});

app().addEventListener("dragleave", (ev) => {
  (ev.target as HTMLElement).closest<HTMLElement>(".item")?.classList.remove("drag-over");
});

app().addEventListener("drop", (ev) => {
  ev.preventDefault();
  const el = (ev.target as HTMLElement).closest<HTMLElement>(".item");
  el?.classList.remove("drag-over");
  const targetId = el?.dataset.id;
  if (!draggedId || !targetId || draggedId === targetId) return;
  // Drop in the upper third = before, lower third = after, middle = inside.
  const rect = el!.getBoundingClientRect();
  const offset = (ev as DragEvent).clientY - rect.top;
  const third = rect.height / 3;
  const position: "before" | "after" | "inside" = offset < third ? "before" : offset > 2 * third ? "after" : "inside";
  void commit(moveItem(doc, draggedId, targetId, position));
  draggedId = null;
});
```

- [ ] **Step 6: Build to confirm the bundle compiles**

Run: `cd tools/roadmap-dashboard && npm run build`
Expected: esbuild writes `dist/bundle.js` with no errors.

- [ ] **Step 7: Manual smoke — drag to reorder**

Run `npm run dashboard`. Drag v7 above v6 (drop in its upper third) → order changes and persists. Drag a point release onto a different version's middle third → it re-parents. Reload to confirm persistence, then `git checkout tools/roadmap-dashboard/roadmap.json` to discard smoke edits.

- [ ] **Step 8: Commit**

```bash
git add tools/roadmap-dashboard/src/reorder.ts tools/roadmap-dashboard/src/reorder.test.ts \
        tools/roadmap-dashboard/src/main.ts
git commit -m "feat(roadmap): drag-to-reorder and re-parent with safe move transform"
```

---

### Task 7: Decompose-with-Claude handoff

**Files:**
- Create: `tools/roadmap-dashboard/server/decompose.ts`
- Test: `tools/roadmap-dashboard/server/decompose.test.ts`
- Modify: `tools/roadmap-dashboard/server/server.ts` (add the `POST /api/decompose` route)
- Modify: `tools/roadmap-dashboard/src/main.ts` (replace the `onDecompose` placeholder)

**Interfaces:**
- Consumes: `RoadmapDoc`, `RoadmapItem` from `../src/types.ts`; `loadRoadmap` from `./store.ts`.
- Produces: `buildDecomposeRequest(doc, itemId): { itemId, title, ancestry, currentChildren, prompt } | null`; `writeDecomposeRequest(dir, request): Promise<string>` (returns the written file path). Server route `POST /api/decompose` body `{ itemId }` → 200 `{ prompt, file }` or 404.

- [ ] **Step 1: Write the failing decompose test**

`tools/roadmap-dashboard/server/decompose.test.ts`:
```ts
import { test } from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, readFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { buildDecomposeRequest, writeDecomposeRequest } from "./decompose.ts";
import type { RoadmapDoc } from "../src/types.ts";

function doc(): RoadmapDoc {
  return {
    meta: { product: "Bernie", tagline: "t", currentVersion: "5", nextStep: "n", lastUpdated: "2026-06-20", schemaVersion: 1 },
    items: [
      { id: "v8", title: "v8 — Ricky: The FX Section", status: "planned", kind: "version",
        children: [{ id: "v8-aux", title: "aux chains", status: "planned", kind: "point" }] },
    ],
    continuousThreads: [],
  };
}

test("buildDecomposeRequest returns the item, its children, and a prompt referencing roadmap.json", () => {
  const req = buildDecomposeRequest(doc(), "v8");
  assert.ok(req);
  assert.equal(req!.itemId, "v8");
  assert.match(req!.title, /Ricky/);
  assert.equal(req!.currentChildren.length, 1);
  assert.match(req!.prompt, /roadmap\.json/);
  assert.match(req!.prompt, /"v8"/);
});

test("buildDecomposeRequest returns null for an unknown id", () => {
  assert.equal(buildDecomposeRequest(doc(), "nope"), null);
});

test("writeDecomposeRequest writes <itemId>.json into the dir", async () => {
  const dir = await mkdtemp(join(tmpdir(), "rm-dec-"));
  const req = buildDecomposeRequest(doc(), "v8")!;
  const file = await writeDecomposeRequest(dir, req);
  assert.match(file, /v8\.json$/);
  const parsed = JSON.parse(await readFile(file, "utf8"));
  assert.equal(parsed.itemId, "v8");
});

test("writeDecomposeRequest sanitizes ids so they cannot escape the dir", async () => {
  const dir = await mkdtemp(join(tmpdir(), "rm-dec-"));
  const req = { itemId: "../../etc/passwd", title: "x", ancestry: [], currentChildren: [], prompt: "p" };
  const file = await writeDecomposeRequest(dir, req);
  assert.ok(file.startsWith(dir), `expected ${file} to stay under ${dir}`);
});
```

- [ ] **Step 2: Run the decompose test to verify it fails**

Run: `cd tools/roadmap-dashboard && node --test server/decompose.test.ts`
Expected: FAIL — `Cannot find module './decompose.ts'`.

- [ ] **Step 3: Implement the decompose request builder/writer**

`tools/roadmap-dashboard/server/decompose.ts`:
```ts
import { writeFile, mkdir } from "node:fs/promises";
import { join } from "node:path";
import type { RoadmapDoc, RoadmapItem } from "../src/types.ts";

export interface DecomposeRequest {
  itemId: string;
  title: string;
  ancestry: string[];
  currentChildren: RoadmapItem[];
  prompt: string;
}

function locate(items: RoadmapItem[], id: string, trail: string[]): { item: RoadmapItem; ancestry: string[] } | null {
  for (const it of items) {
    const here = [...trail, it.id];
    if (it.id === id) return { item: it, ancestry: here };
    if (it.children) { const f = locate(it.children, id, here); if (f) return f; }
  }
  return null;
}

export function buildDecomposeRequest(doc: RoadmapDoc, itemId: string): DecomposeRequest | null {
  const found = locate(doc.items, itemId, []);
  if (!found) return null;
  const { item, ancestry } = found;
  const prompt =
    `Decompose roadmap item ${item.id} ("${item.title}") into 4–8 concrete, independently ` +
    `testable tasks. Read tools/roadmap-dashboard/roadmap.json, append each task as a child ` +
    `(kind "task", status "planned") of the item with id "${item.id}", preserving its existing ` +
    `children, then save the file (atomic write). Keep titles short; put detail in each task's ` +
    `"summary". Do not change any other item.`;
  return { itemId: item.id, title: item.title, ancestry, currentChildren: item.children ?? [], prompt };
}

function safeName(id: string): string {
  return id.replace(/[^a-zA-Z0-9._-]/g, "_");
}

export async function writeDecomposeRequest(dir: string, req: DecomposeRequest): Promise<string> {
  await mkdir(dir, { recursive: true });
  const file = join(dir, `${safeName(req.itemId)}.json`);
  await writeFile(file, JSON.stringify(req, null, 2) + "\n", "utf8");
  return file;
}
```

- [ ] **Step 4: Run the decompose test to verify it passes**

Run: `cd tools/roadmap-dashboard && node --test server/decompose.test.ts`
Expected: PASS — 4 tests pass.

- [ ] **Step 5: Add the route to the server**

In `tools/roadmap-dashboard/server/server.ts`, add the import near the top:
```ts
import { buildDecomposeRequest, writeDecomposeRequest } from "./decompose.ts";
```
Then add this route handler immediately after the `PUT /api/roadmap` block (before the static-file section):
```ts
      if (path === "/api/decompose" && req.method === "POST") {
        const raw = await readBody(req);
        const { itemId } = JSON.parse(raw) as { itemId?: string };
        const doc = await loadRoadmap(opts.roadmapPath);
        const request = itemId ? buildDecomposeRequest(doc, itemId) : null;
        if (!request) return send(res, 404, MIME[".json"], JSON.stringify({ error: "item not found" }));
        const dir = join(opts.rootDir, "requests");
        const file = await writeDecomposeRequest(dir, request);
        return send(res, 200, MIME[".json"], JSON.stringify({ prompt: request.prompt, file }));
      }
```

- [ ] **Step 6: Extend the server test for the new route**

Append to `tools/roadmap-dashboard/server/server.test.ts`:
```ts
test("POST /api/decompose returns a prompt and writes a request file", async () => {
  // The seed test doc only has v5; add a child-bearing item via PUT first.
  const cur = await (await fetch(`${base}/api/roadmap`)).json() as RoadmapDoc;
  cur.items.push({ id: "v8", title: "v8 — Ricky", status: "planned", kind: "version" });
  await fetch(`${base}/api/roadmap`, { method: "PUT", headers: { "content-type": "application/json" }, body: JSON.stringify(cur) });

  const r = await fetch(`${base}/api/decompose`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ itemId: "v8" }),
  });
  assert.equal(r.status, 200);
  const body = await r.json() as { prompt: string; file: string };
  assert.match(body.prompt, /roadmap\.json/);
  assert.match(body.file, /v8\.json$/);
});

test("POST /api/decompose 404s for an unknown item", async () => {
  const r = await fetch(`${base}/api/decompose`, {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ itemId: "nope" }),
  });
  assert.equal(r.status, 404);
});
```

- [ ] **Step 7: Run the full server suite to verify it passes**

Run: `cd tools/roadmap-dashboard && node --test server/server.test.ts`
Expected: PASS — 6 tests pass (4 original + 2 new). The `requests/` dir is created under the tool root; it is git-ignored.

- [ ] **Step 8: Replace the client `onDecompose` placeholder**

In `tools/roadmap-dashboard/src/main.ts`, replace the placeholder function with the real handoff:
```ts
async function onDecompose(id: string): Promise<void> {
  const r = await fetch("/api/decompose", {
    method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ itemId: id }),
  });
  if (!r.ok) { alert(`Decompose failed: ${r.status}`); return; }
  const { prompt, file } = await r.json() as { prompt: string; file: string };
  try { await navigator.clipboard.writeText(prompt); } catch { /* clipboard may be blocked; prompt still shown */ }
  alert(
    `Request written to:\n${file}\n\n` +
    `The decomposition prompt has been copied to your clipboard — paste it into Claude Code.\n\n` +
    `Prompt:\n${prompt}\n\n` +
    `When Claude finishes, reload this page to see the new tasks.`,
  );
}
```

- [ ] **Step 9: Build and manually smoke the handoff**

Run: `cd tools/roadmap-dashboard && npm run build && npm run dashboard`. Click ⚡ on v8. Expected: an alert showing the request-file path + the copied prompt; `tools/roadmap-dashboard/requests/v8.json` now exists. Verify the JSON contains `itemId`, `currentChildren`, and `prompt`. Stop the server. (`requests/` is git-ignored, so nothing to clean up in git.)

- [ ] **Step 10: Commit**

```bash
git add tools/roadmap-dashboard/server/decompose.ts tools/roadmap-dashboard/server/decompose.test.ts \
        tools/roadmap-dashboard/server/server.ts tools/roadmap-dashboard/server/server.test.ts \
        tools/roadmap-dashboard/src/main.ts
git commit -m "feat(roadmap): decompose-with-Claude handoff via request file + clipboard"
```

---

### Task 8: Re-engineer `phases.md` and `README.md`

**Files:**
- Modify (rewrite): `docs/roadmap/phases.md`
- Modify: `docs/roadmap/README.md`

**Interfaces:** none (documentation). The acceptance bar: after this task, no version/status/ordering table exists outside `roadmap.json`; `phases.md` holds only the durable vision + a pointer.

- [ ] **Step 1: Rewrite `phases.md`**

Replace the entire contents of `docs/roadmap/phases.md` with:
```markdown
# Phase plan

> **The live roadmap — every version, status, ordering, point release, and task —
> lives in [`tools/roadmap-dashboard/roadmap.json`](../../tools/roadmap-dashboard/roadmap.json)
> and is viewed/edited via the roadmap dashboard:**
>
> ```bash
> cd tools/roadmap-dashboard && npm install && npm run dashboard   # http://localhost:4173
> ```
>
> This document holds only the **durable vision and engine principles** — the things
> that do not change release to release. It deliberately carries **no** status tables,
> so it can never drift from the dashboard. (Brought into this form 2026-06-20; see the
> [dashboard design spec](../superpowers/specs/2026-06-20-roadmap-dashboard-design.md).)

## End-state vision (re-positioned to K2061/K2088, 2026-06-16)

The plugin is a **K2061/K2088-class VAST engine bracketed by a constant Summit analog
voice.** Sound *generation* is fully flexible (K2061 Dynamic VAST); sound *shaping* is
always a Summit.

- **Flexibility from K2061/K2088 VAST** — Dynamic VAST: build sound from arbitrary
  serial/parallel DSP graphs where every source (Summit oscillators, KVA, FM, wavetable,
  noise — and later samples) is just a block. 32 layers, Multis, KDFX, Cascade.
- **A constant Summit voice** — a **selectable, live-switchable filter model** (Huggett
  default; Moog and Oberheim SEM later) + drive → VCA, and the modulation system
  (amp/mod envelopes, LFOs, mod matrix, voice modes), are **always present** and always
  live. You can never reach a dead control or a patch that isn't a real synth.
- **Immediacy from Summit** — the constant spine is the permanent front panel; the
  variable source/DSP region's knob-clusters swap to match the active blocks. Tiered
  immediacy: front panel for live params, pages for the long tail.

See the [v4.5(C) re-positioning spec](../specs/2026-06-16-v4.5-k2061-repositioning-design.md)
and the living [engine architecture register](../architecture/engine-questions.md).

## Product naming

The shipping synthesizer is **Bernie** (repo codename `k2000` stays). Bernie's built-in
effects section is **Ricky** — a Summit/KDFX-style multi-FX block reached via an
*Advanced* button on Bernie's front panel (Arturia-style), inserted **after the amp/VCA**,
with a subset of its FX blocks also exposed as VAST DSP blocks (roadmap item **v8**).

## Engine principles (cross-cutting)

- **The model:** `[ K2061 Dynamic VAST source + DSP graph (variable) ] → [ constant
  Summit spine: selectable filter model (Huggett default) + drive → VCA, with
  envelopes/LFOs/mod matrix/voice modes ]`, **per voice, per Layer**.
- **Locked decisions** (from the register): **full stereo throughout** · **256-voice**
  target · spine + modulation **per-Layer** · **synth-only** sources now (sample/keymap
  arrive later).
- **GUI grows with the engine, toward a fixed aesthetic** — no phase ships a feature you
  can't drive; each phase advances the visual design incrementally toward the target
  Summit aesthetic rather than deferring a "real GUI" to the end.
- **Performance is a gate** — at 256 voices × full stereo × graph DSP, every phase meets a
  per-voice CPU budget with profiling as a release gate.

## Cross-cutting threads

These are continuous, not version-pinned (tracked in the dashboard's "Continuous
threads"): the **DSP test harness** (grows to cover every component, gates releases) · the
**per-voice perf gate** · the **incremental GUI** toward the target aesthetic · the
**security-scan CI baseline**.

## Cmajor — a decision gate, not a version

Cmajor (graph-based DSP language) is evaluated as a **spike before v6**: pilot one filter
model, verify JUCE integration, prove the 256-voice per-voice model, then write an ADR.
The full migration's position is decided by that spike — if it wins, the v6 graph is
authored in Cmajor (avoiding a double build); if not, the C++ DSP stays. This is why the
spike must resolve **before v6 is designed**.

## What this is *not*

- A commitment. Ordering can shift if a downstream phase reveals an upstream one was
  over- or under-scoped. The dashboard's `firmness` field marks v12+ as tentative.
- A deadline. No dates on unshipped work.
- Permission to scope-creep an earlier phase. Each version only ships what it scopes.

## How a phase becomes real

1. Enumerate the phase's open questions into the
   [engine architecture register](../architecture/engine-questions.md); ask them; record
   answers; **groom the register for internal consistency**.
2. Write `specs/YYYY-MM-DD-vN-<theme>-design.md` once the relevant questions are resolved.
3. Capture non-obvious decisions as ADRs in [`../decisions/`](../decisions/).
4. Add architecture deep dives in [`../architecture/`](../architecture/) for load-bearing
   subsystems.
5. Update the phase's status in the dashboard (`roadmap.json`).
```

- [ ] **Step 2: Update `README.md`**

Replace the file table in `docs/roadmap/README.md` so it points at the dashboard. Set the body to:
```markdown
# Roadmap

The **live roadmap** (every version, status, ordering, point release, and task) lives in
[`tools/roadmap-dashboard/roadmap.json`](../../tools/roadmap-dashboard/roadmap.json) and is
viewed/edited with the dashboard:

```bash
cd tools/roadmap-dashboard && npm install && npm run dashboard   # http://localhost:4173
```

## Files

| Doc | What it covers |
|---|---|
| [phases.md](phases.md) | The durable end-state **vision** + engine principles only — no status tables (those live in the dashboard). |
| [v2-known-concerns.md](v2-known-concerns.md) | Issues identified in the v1 final review to be addressed in v2 or later. |
```

- [ ] **Step 3: Verify no stray status tables remain**

Run: `grep -nE '^\| *\*\*v[0-9]' docs/roadmap/phases.md || echo "clean: no version-status table rows in phases.md"`
Expected: prints `clean: no version-status table rows in phases.md`.

- [ ] **Step 4: Commit**

```bash
git add docs/roadmap/phases.md docs/roadmap/README.md
git commit -m "docs(roadmap): re-engineer phases.md to vision-only; dashboard is the live roadmap"
```

---

### Task 9: Tool README + full-suite green + final smoke

**Files:**
- Create: `tools/roadmap-dashboard/README.md`

**Interfaces:** none.

- [ ] **Step 1: Write the tool README**

`tools/roadmap-dashboard/README.md`:
```markdown
# Bernie Roadmap Dashboard

A compact, project-only roadmap board (think a tiny Jira/ClickUp, no social cruft).
`roadmap.json` is the single source of truth for Bernie's roadmap — version, status,
ordering, point releases, and tasks. It is git-tracked; the dashboard reads and writes it.

## Run

```bash
npm install        # once — installs esbuild (the only dependency)
npm run dashboard  # builds the UI and serves http://localhost:4173
```

Requires **Node 24+** (native TypeScript type-stripping + built-in test runner).

## What it does

- Renders the roadmap as a tree: Version → point release → feature → task.
- Inline **add / edit / delete**; click a title to rename, click again to cycle status.
- **Drag to reorder** within a level, or drop on an item's middle third to re-parent.
- **Progress bars** roll up from leaf-task completion (or a manual `progressOverride`).
- **⚡ Decompose with Claude**: writes `requests/<id>.json` and copies a prompt to your
  clipboard. Paste it into Claude Code; Claude appends the task breakdown to that item in
  `roadmap.json`; reload to see it.

## Tests

```bash
npm test   # node --test across server/ and src/
```

## Layout

- `roadmap.json` — the data (committed).
- `server/` — `store.ts` (atomic load/save + schema guard), `server.ts` (HTTP),
  `decompose.ts` (Claude handoff).
- `src/` — `types.ts`, `progress.ts` (roll-up), `render.ts` (tree → HTML), `edit.ts` +
  `reorder.ts` (pure transforms), `api.ts`, `main.ts` (bootstrap), `styles.css`.
- `requests/` — decompose hand-off drop-box (git-ignored).
```

- [ ] **Step 2: Run the entire test suite green**

Run: `cd tools/roadmap-dashboard && node --test`
Expected: PASS — all suites (progress, store, server, render, edit, reorder, decompose) pass, 0 failures.

- [ ] **Step 3: Final end-to-end smoke**

Run: `cd tools/roadmap-dashboard && npm run dashboard`. Verify in the browser: header/“you are here”, the full v1–v25 + Cmajor tree, expand/collapse, edit a title, add a sub-item, drag-reorder, ⚡ decompose writes a request. Then discard any data edits: `git checkout tools/roadmap-dashboard/roadmap.json`.

- [ ] **Step 4: Commit**

```bash
git add tools/roadmap-dashboard/README.md
git commit -m "docs(roadmap): tool README for the dashboard"
```

---

## Self-Review

**Spec coverage:**
- §2 decisions → Task 1 (types/erasable TS), Task 2 (canonical `roadmap.json` + schema), Task 3 (Node server), Task 7 (clipboard/file Claude handoff, no API), Task 1 (esbuild-only). ✓
- §3 roadmap content → Task 2 seed (v1–v25, renumbered v5 points, Cmajor gate+conditional, continuous threads). ✓
- §4 data model → Task 1 `types.ts`; progress roll-up Task 1. ✓
- §5 architecture/file structure → Tasks 1–7 create exactly the §5 files. ✓
- §6 decompose handoff loop → Task 7. ✓
- §7 phases.md re-engineering → Task 8 (with the grep acceptance check). ✓
- §8 testing → each logic module has a `node --test` suite; manual smokes called out. ✓
- §9 out-of-scope → nothing in the plan adds auth/API/cloud/gantt. ✓
- §10 build sequence → Tasks 1–9 follow it (scaffold→types/progress→seed/store→server→render→edit→reorder→decompose→docs). ✓

**Placeholder scan:** no "TBD/TODO/handle appropriately"; every code step shows complete code; the Task 5 `onDecompose` stub is explicitly a named placeholder *replaced* in Task 7 Step 8 (not a plan gap). ✓

**Type consistency:** `RoadmapDoc`/`RoadmapItem`/`Status`/`ItemKind`/`Tag`/`Thread` defined in Task 1 and consumed verbatim throughout; `computeProgress`, `findItem`, `moveItem(…,"before"|"after"|"inside")`, `buildDecomposeRequest`, `writeDecomposeRequest`, `getRoadmap`/`putRoadmap` signatures match across the tasks that define and call them. The item count check in Task 2 Step 6 (28) = v1,v2,v3,v4,v4.5 (5) + v5…v25 (21) + cmajor-spike + cmajor-migration (2) = 28. ✓
