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

## Franklin tab

Beside **Roadmap**, a **Franklin** tab shows Franklin's runs (device
characterization + the test suite) live: running/stalled cards with progress,
a CI strip, a new-run form (whitelisted templates only), and an archive with a
deviations-first run-detail view. Each test expands to a six-field info card
(what/purpose/compares/on-success/on-failure/**who ran it** — you, Claude, or CI),
and a searchable **Catalog** section browses all test cards. See
`docs/franklin/dashboard.md` for the full operator's manual and
`docs/franklin/charter.md` for what Franklin is.

Runs record to `.franklin/runs/*.ndjson` the moment a Franklin producer starts —
**even when this dashboard isn't running.** The dashboard only reads and displays
what's already on disk; starting/stopping the server never affects whether a run
is captured.

Two independent poll cadences drive the tab: run state (`/api/runs`) refreshes
every **2 s**, CI status (`/api/ci`, via `gh`) refreshes every **60 s** server-side
and is polled by the frontend every **10 s**.

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
