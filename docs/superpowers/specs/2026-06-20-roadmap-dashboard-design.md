# Roadmap Dashboard + Roadmap Re-engineering — Design

**Version:** 1.0 (artifact)
**Date:** 2026-06-20
**Status:** Approved (brainstorm) — pending spec review
**Scope:** A rich, project-only roadmap dashboard (compact Jira/ClickUp) backed by a
single git-tracked data file, plus a clean rewrite of the v5→v25 roadmap and the
re-engineering of `docs/roadmap/phases.md` to match.

---

## 1. Problem

`docs/roadmap/phases.md` has sprawled. It interleaves shipped history (v1–v4), a v5
point-release table whose numbers explicitly **do not** track build order, a separate
"v5 build sequence", follow-up tables, far-future mentions (v14 VST rig, Cmajor), and
the end-state vision. There is no single at-a-glance "where are we / what's next" view,
and the plan stops at v11+. The user is losing track of project state.

Two coupled deliverables solve this:

1. **A new, clean roadmap** — major-version-per-theme, v5 through v25, with v5 point
   releases renumbered to match build order, and the scattered cross-cutting concerns
   pulled into an explicit band.
2. **A rich dashboard** — a compact, project-oriented editor (between Jira and ClickUp,
   no social/collaboration cruft) that renders the roadmap, supports reorder / add /
   edit / sub-items / progress bars, and can hand a roadmap item to Claude Code for task
   decomposition.

The dashboard's data file becomes the **single source of truth**; `phases.md` is
re-engineered into a thin pointer so the competing tables can never drift again.

---

## 2. Decisions (resolved 2026-06-20)

All settled during brainstorming:

- **Source of truth:** the dashboard's `roadmap.json` is canonical. `phases.md` is
  rewritten to a thin pointer + narrative vision (no competing status tables).
- **Runtime:** a tiny zero-framework **Node server** (`npm run dashboard`) that reads and
  writes the git-tracked `roadmap.json` and serves the bundled UI. (This supersedes the
  earlier "single static file, no server" idea, which cannot support real editing,
  repo persistence, or the Claude handoff.)
- **Claude integration:** the "decompose" button **hands off through the existing Claude
  Code session** — no API key, no extra billing. The button writes a request file and
  surfaces a ready-to-paste prompt; Claude writes the task breakdown back into
  `roadmap.json`.
- **Granularity:** major version = one theme; point releases and tasks nest under it.
- **Far future (v12–v25):** concrete themes, but flagged `tentative` (planning intent,
  not commitment).
- **Cmajor:** *not* a version. Split into a near-term **Cmajor Spike (decision gate,
  pre-v6)** and a floating **Cmajor Migration (conditional, slot decided by the spike)**.
- **Language/build:** UI authored in **TypeScript**, bundled with **esbuild** (one dev
  dependency). The tool lives in `tools/roadmap-dashboard/`, fully isolated from the
  C++/CMake build.

---

## 3. The roadmap content (seed for `roadmap.json`)

### 3.1 Shipped history (collapsed by default in the UI)

| v | Theme | Shipped |
|---|---|---|
| v1 | Skeleton end-to-end | 2026-05-30 |
| v2 | Layer abstraction | 2026-06-14 |
| v3 | Algorithm abstraction | 2026-06-15 |
| v4 | Multi-Layer Programs | 2026-06-16 |
| v4.5 | K2061 re-positioning + Summit UI foundation | 2026-06-19 (KB OCR fix outstanding) |

### 3.2 v5 — The Constant Summit Spine (🔵 in progress)

Point releases **renumbered so number == build order**:

| Release | Theme | Status |
|---|---|---|
| v5.0 | Nonlinear Huggett (plain-tanh spine) | ✅ shipped (remediated 2026-06-20) |
| v5.1 | Q17 click-free model hot-swap (heap-free in-place state) | next |
| v5.2 | Real Separation / dual-filter (the broken Huggett signature) | specced |
| v5.3 | HQ oversampling tiers + on-screen keyboard | specced |
| v5.4 | Moog ladder (2nd filter model) | specced |
| v5.5 | Oberheim SEM (3rd filter model) | planned |

(The DSP test harness is already shipped; it is a Continuous Thread, not a point release.)

### 3.3 Major versions v6 → v25

v6–v11 **firm**; v12–v25 **tentative**.

| v | Theme | One-line deliverable |
|---|---|---|
| v6 | Dynamic VAST Graph Routing | Wired serial+parallel per-voice DSP graph; visual wiring editor. |
| v7 | Source & DSP Block Library | Osc per-waveform mini-mixer, KVA, noise; DSP blocks (mixer, ring mod, shapers, EQ). |
| v8 | Ricky — The FX Section | Post-VCA multi-FX + aux chains; a subset also exposed as VAST blocks. |
| v9 | Multipart, Cascade & Multis | A/B/Split/Layer surface; toward 32 layers / 256 voices; Multis ≤16. |
| v10 | FM Layers | 6-operator FM source + operator panel. |
| v11 | Sampling & Keymaps | Sample/keymap sources (the big v11+ piece, decomposed out). |
| v12 | Preset System & Patch Management | Browser, tagging, compare/init, patch morph. |
| v13 | Commercialization I | Demo mode + license unlock + macOS port. |
| v14 | Commercial-VST Comparison & RE Rig | Offline-host a commercial VST, capture, compare through the metric catalog. |
| v15 | Filter Model Expansion | Diode ladder + more topologies; gates re-anchored to hardware. |
| v16 | Modulation Deepening | Expanded mod matrix, more LFO/env, advanced voice modes. |
| v17 | Performance Layer | Arpeggiator, sequencer, macros/controllers. |
| v18 | MPE & Per-Note Expression | Per-note pitch/pressure/slide. |
| v19 | Advanced Sources | Wavetable / additive / granular deepening. |
| v20 | The Final Aesthetic | Full target GUI, theming, hi-DPI/scalable UI. |
| v21 | Preset Ecosystem & Factory Library | Packs, sharing, curated factory sounds. |
| v22 | Performance & Scale Hardening | 256-voice everywhere, SIMD, multicore. |
| v23 | Platform Expansion | AU/AUv3, CLAP, more hosts/formats. |
| v24 | AI-Assisted Sound Design | Local-LLM patch generation/morphing. |
| v25 | Bernie 1.0 — Commercial Flagship Release | Everything converges; 1.0 ship. |

### 3.4 Gates & continuous threads (not version rows)

- **Cmajor Spike** — decision gate, must resolve **before v6 is designed**. Pilot one
  filter model in Cmajor; verify JUCE integration; prove the 256-voice model. → ADR.
- **Cmajor Migration** — `conditional`: position decided by the spike. If it wins, v6 is
  authored in Cmajor (no double-build); if not, C++ stays.
- **Continuous Threads** — DSP test-harness coverage · per-voice perf gate · incremental
  GUI toward the target aesthetic · security-scan CI baseline.

---

## 4. Data model (`roadmap.json`)

A single JSON document. Top level:

```jsonc
{
  "meta": {
    "product": "Bernie",
    "tagline": "...",
    "currentVersion": "5",
    "nextStep": "v5.1 — Q17 click-free model hot-swap",
    "lastUpdated": "2026-06-20",
    "schemaVersion": 1
  },
  "items": [ /* RoadmapItem[] — ordered */ ],
  "continuousThreads": [ /* Thread[] */ ]
}
```

`RoadmapItem` (recursive — Version → Sub-item → Task):

```ts
interface RoadmapItem {
  id: string;              // stable, e.g. "v5", "v5.1", "v8-ricky-aux"
  title: string;
  status: Status;          // see below
  kind: ItemKind;          // "version" | "point" | "feature" | "task"
  summary?: string;        // one-line deliverable
  notes?: string;          // longer freeform
  tags?: Tag[];            // "keystone" | "gate" | "conditional" | "continuous"
  shipped?: string;        // ISO date, if shipped
  firmness?: "firm" | "tentative";
  specLinks?: string[];    // repo-relative paths/URLs
  progressOverride?: number; // 0..100, optional manual override
  children?: RoadmapItem[];  // ordered; reorder persists this array
}

type Status = "shipped" | "in-progress" | "specced" | "planned" | "tentative" | "blocked";
type ItemKind = "version" | "point" | "feature" | "task";
type Tag = "keystone" | "gate" | "conditional" | "continuous";
```

**Progress roll-up:** if `progressOverride` is set, use it. Else compute from leaf
descendants: `progress = shippedLeaves / totalLeaves * 100`, where a leaf counts as done
when `status === "shipped"`. An item with no children uses its own status (shipped = 100,
in-progress = 50, else 0). This is a pure function of the tree; the UI never stores derived
progress.

**Ordering is positional** — the array order in `children` *is* the roadmap order. Drag
-reorder rewrites the array; the server persists the whole document.

---

## 5. Architecture

```
tools/roadmap-dashboard/
  package.json            # esbuild devDep; "dashboard" + "build" scripts
  tsconfig.json
  roadmap.json            # SOURCE OF TRUTH (git-tracked)
  server/
    server.ts             # zero-framework Node http server
    store.ts              # load/save roadmap.json (atomic write + schema guard)
  src/
    main.ts               # bootstrap, fetch roadmap, mount
    render.ts             # tree → DOM (cards, progress bars, badges)
    edit.ts               # inline add/edit/delete
    reorder.ts            # drag-and-drop, re-parent, persist
    progress.ts           # pure roll-up function (unit-tested)
    decompose.ts          # "decompose with Claude" handoff
    api.ts                # typed fetch wrappers
    types.ts              # shared RoadmapItem/Status types
    styles.css            # compact dark Summit-panel aesthetic
  index.html              # shell; bundle injected by esbuild
  requests/               # decompose handoff drop-box (git-ignored)
```

**Server** (no framework, Node `http`):
- `GET  /api/roadmap` → current `roadmap.json`.
- `PUT  /api/roadmap` → validate (schemaVersion + shape) then atomic-write the document.
- `POST /api/decompose` → body `{ itemId }`; writes `requests/<itemId>.json` with the
  item + its ancestry + a generated prompt; returns the prompt text for the UI to show.
- `GET  /` and static assets → serves `index.html` + the esbuild bundle.

`npm run dashboard` = `esbuild` bundle → start server → open `http://localhost:<port>`.

**Build isolation:** the tool is pure Node/TS under `tools/`. It is never referenced by
the C++ `CMakeLists.txt`. `node_modules/` and `requests/` are git-ignored.

---

## 6. The Claude decomposition handoff (the novel part)

The loop keeps Claude in the existing Claude Code session — no API key, no autonomous
calls, user stays in the loop:

1. User clicks **Decompose** on a roadmap item (e.g. v8 Ricky).
2. UI calls `POST /api/decompose { itemId: "v8" }`.
3. Server writes `tools/roadmap-dashboard/requests/v8.json`:
   ```jsonc
   {
     "itemId": "v8",
     "title": "Ricky — The FX Section",
     "ancestry": ["v8"],
     "currentChildren": [ /* existing sub-items */ ],
     "prompt": "Decompose roadmap item v8 (Ricky — The FX Section) into 4–8 concrete
                tasks. Read tools/roadmap-dashboard/roadmap.json, append the tasks as
                children of item id \"v8\", preserve existing children, then save."
   }
   ```
4. UI shows the prompt with a **Copy** button: *"Paste this into Claude Code."*
5. User pastes; Claude reads the request + `roadmap.json`, writes `task` children under
   the item, saves the file.
6. User refreshes (or the UI polls `GET /api/roadmap`); the new tasks appear, progress
   bars recompute.

This is a *file + clipboard* handoff, deliberately not an automated API call — it reuses
the paid session, is reviewable, and never runs unsupervised. (Rationale: a prior session
hit a monthly API spend limit; we avoid a second billed channel.)

---

## 7. `phases.md` re-engineering (first-class deliverable)

`docs/roadmap/phases.md` is **rewritten**, not patched:

- Keep: the **end-state vision**, **engine principles**, product naming (Bernie/Ricky),
  "what this is not", and "how a phase becomes real" — the durable narrative.
- Remove: the v5 point-release table, the separate "v5 build sequence", the v5.0
  follow-up tables, and the inline far-future scatter — all of which now live in
  `roadmap.json` / the dashboard.
- Add: a prominent pointer — *"The live roadmap (status, ordering, tasks) is
  `tools/roadmap-dashboard/roadmap.json`, viewed via `npm run dashboard`. This document
  holds only the durable vision and principles."*
- `docs/roadmap/README.md` updated to point at the dashboard as the status surface.

The acceptance bar: after the rewrite there is exactly **one** place that holds
version/status/ordering (the dashboard data), and `phases.md` cannot drift from it because
it no longer carries that data.

---

## 8. Testing

- **`progress.ts`** roll-up: unit-tested (pure function) — empty tree, all-shipped,
  mixed, override, nested.
- **`store.ts`**: round-trip load→save→load equality; rejects malformed/old schema;
  atomic write (temp + rename) so a crash can't truncate `roadmap.json`.
- **Server**: GET returns seed; PUT persists and is reflected by the next GET; PUT of an
  invalid body is rejected with the file unchanged; decompose writes a request file.
- **Reorder/edit**: a thin DOM-level test that a reorder produces the expected `children`
  array sent to PUT (logic extracted from event handlers so it's testable without a real
  drag).
- Manual smoke: `npm run dashboard`, verify render, edit, reorder-persist, decompose
  handoff round-trip.

No C++ test suite changes — this tool is outside that build.

---

## 9. Out of scope (YAGNI)

- Authentication, multi-user, comments, activity feeds, social features.
- Live Anthropic API integration / autonomous decomposition.
- Cloud sync / hosting — it's a local, single-user, git-versioned tool.
- Gantt charts, time tracking, due dates/calendars.
- Editing C++/DSP from the dashboard.

---

## 10. Build sequence (for the implementation plan)

1. Scaffold `tools/roadmap-dashboard/` (package.json, tsconfig, esbuild, gitignore).
2. `types.ts` + `progress.ts` (+ its unit tests).
3. Author `roadmap.json` seed from §3.
4. `store.ts` + server (`GET/PUT /api/roadmap`) + tests.
5. UI render (tree → cards, badges, progress bars) + styles.
6. Inline edit (add/edit/delete) → PUT.
7. Drag reorder / re-parent → PUT.
8. Decompose handoff (`POST /api/decompose` + request file + Copy-prompt UI).
9. Re-engineer `phases.md` + `README.md` (§7).
10. Manual smoke + README for the tool.
