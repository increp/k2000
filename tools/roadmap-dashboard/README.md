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
