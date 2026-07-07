# Purpose-Driven Characterization Grids — Design (resolves Q28, engagement item 7)

**Version:** 5.23 (artifact) · **Date:** 2026-07-07 · **Status:** Approved in brainstorm (user rulings 2026-07-07); build follows
**Register:** resolves **Q28** · **Parent:** SP-A core (`2026-07-01-device-characterization-core-design.md`), Franklin dashboard (`2026-07-03`/`2026-07-05` specs)

---

## 1. Problem and rulings

`chz::fullGrid()` crosses every axis with every other: 36,000 raw points/model (29,088 supported), measured at **≈39–41 h per model** — dead weight as a routine instrument. The user ruled (2026-07-07, register Q28's two deferred questions):

- **(a) Purposes: ALL FOUR** — SP-D hardware-comparison map · OS/aliasing verification · host-rate portability · large-signal/drive law.
- **(b) Budget: ~2 hours** for the deep run (the 126-point `quick` grid stays the CI/smoke gate).

Design rule that follows: each purpose gets a sub-grid dense only along the axes its purpose needs; axes no purpose claims are dropped, not thinned.

## 2. Empirical cost model (from Franklin runlogs, 96 kHz, 200-probe B1+B2+B3 points)

| condition | s/point | source |
|---|---|---|
| os1 | 1.64 | n=53 |
| os2 | 1.99 | n=101 |
| os4 | 3.05 | n=101 |
| os8 | 5.07 | n=100 |

Scaling: probe count ≈ linear (700-probe fullGrid points averaged ≈3.5× the 200-probe quick points); host rate ≈ linear (192k ≈ 2× 96k, 44.1k ≈ 0.46×). Model uncertainty is fine: every run self-prices live via the ETA line; re-slice if a first real run disagrees materially.

## 3. The four sub-grids (per model)

| id | purpose | axes | points | est. |
|---|---|---|---|---|
| `spd` | SP-D hardware-comparison map — the response map future Summit/Arturia captures are compared against | os {8} · osMode {Live} · rate {96000} · modes all 5 · cutoffs **15** log 50 Hz–16 kHz · res {0, .2, .4, .6, .8, 1.0} · drives {0} · **400 probes** 20 Hz–24 kHz | 450 | **~75 min** |
| `osalias` | aliasing falls as OS rises, at the points where aliasing lives | os {1,2,4,8} · osModes {Live, Render} · rate {96000} · modes {LP24, BP} · cutoffs {4k, 8k, 16k} · res {0.9, 1.0} · drives {0, 1} · 200 probes | 192 | ~10 min |
| `rates` | host-rate invariance spot-check | rates {44100, 48000, 88200, 96000, 192000} · os {1, 8} · osMode {Live} · modes {LP24, HP} · cutoffs {250, 1k, 4k} · res {0, 0.9} · drives {0} · 200 probes | 120 | ~8 min |
| `largesig` | drive/resonance law (Q27/SP-B axis) — the operating-point lattice SP-B's level battery will reuse | os {1, 8} · osMode {Live} · rate {96000} · modes {LP24} · cutoffs {250, 1k, 4k} · res {0, .3, .6, .8, .9, 1.0} · drives {0, .25, .5, .75, 1.0} · 200 probes | 180 | ~10 min |

**Union ≈ 1.7–2.0 h/model.** Decisions the budget forced (explicit, user-approved):
- `spd` runs **drive 0** — drive lives in `largesig`; folding 3 drives into the map triples it to ~4 h.
- `spd` uses **400 probes**, not 700 — 700 alone costs ~2.2 h; 400 doubles quick's resolution, and razor-peak *tip* truth remains the dense disparity sweep's job (`BERNIE_RUN_DISPARITY`), not the map's.
- `spd`'s **96 kHz / os8 Live** is the *working* capture-reference assumption — SP-D hasn't specced the rig. Re-pin when it does (CALIB-style note in code).

## 4. Implementation surface

- **Grid factories** (beside `coarseGrid`/`fullGrid` in `tests/characterization/CharacterizationRunner.cpp`, declared in the header): `Grid spdGrid(); Grid osAliasGrid(); Grid hostRateGrid(); Grid largeSignalGrid();` — axes exactly per §3.
- **CLI** (`characterize_main.cpp`): `--grid quick|full|spd|osalias|rates|largesig|deep` (default `full`, unchanged); `deep` runs the four purpose grids **in sequence** per model (four `run()` calls, one runlog with continuing progress; labels carry the sub-grid id). `--quick` stays as an alias for `--grid quick`. Grid name flows into the runlog `start.grid` and the stdout digest.
- **Dashboard** (`server/control.ts` + form): chz template `grid` enum widens to the seven names; the form's grid `<select>` gains them (labels: quick ~4 min · deep ~2 h · spd ~75 min · osalias ~10 min · rates ~8 min · largesig ~10 min · full (legacy, ~40 h)). Server-side validation rejects unknown names as today.
- **Tests** (suite): grid-shape assertions per factory (axis contents + computed point count) inside ONE new `beginTest` section (suite 291 → 292; README + catalog + drift updated). CLI parse coverage via the shape test's factory calls (parse itself is trivial switch).
- **Docs:** this spec; register **Q28 → 🟢 resolved** (answers + pointer here); `docs/filter-validation/running.md` grid table; dashboard manual template list.

## 5. Non-goals

- No new batteries: `largesig` selects operating points for the EXISTING B1/B2/B3; the true gain-vs-input-level battery is SP-B's deliverable and will reuse this lattice.
- `fullGrid` is retained (legacy exhaustive, documented as ~40 h) — never the default path of any workflow.
- No golden/gate changes: purpose grids are opt-in instruments; the CI gate stays the quick grid's goldens.

## 6. Verification

Suite 292/0 · drift `--ci` clean · npm green (template enum test) · live smoke: start `rates` (the ~8 min grid) from the dashboard API and run it **to completion** — validates the cost model's shortest prediction end-to-end and exercises `--grid` through the whole stack; `deep` is priced by its own ETA line on first user run.
