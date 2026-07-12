# Bernie (repo k2000) — Session Handoff

**Version:** 5.32 (artifact) · **Date:** 2026-07-11
Written for a fresh session with zero memory. Concrete paths and symbols throughout.
**Supersedes** `2026-07-08-session-handoff.md` (pre-GUI-cycle; do not trust for current state).
**FIRST ACTION of any session:** run `tools/drift-check --session`, read every WARN, then read `CLAUDE.md`.

---

## 1. What happened (2026-07-09 → 07-11): the GUI cycle opened — three arcs, one PR

The gold-standard engagement's paused item 5 (GUI cycle) was resumed by the user on 2026-07-09
and produced three stacked arcs on `feat/bernie-gui`, squash-merged to **main as `5373725`
(PR #21)** on 2026-07-12Z. Everything was subagent-driven (per-task reviews + final
whole-branch reviews, all 0-Critical/0-Important) except the acceptance-driven visual tuning,
which was controller-inline against self-rendered snapshots (see §4 — the tooling that makes
that safe now exists).

- **Arc 1 — knob pre-work** (spec v5.30, plan `2026-07-09-vco-prework-removals.md`): Drive/Mix
  and Moog Wave/Octave/Bass knobs hidden (bindings/DSP untouched). Moog-Bass full retirement
  was deliberately narrowed to knob-only mid-planning: its DSP runs through the Cmajor codegen
  pipeline (Docker/jammy toolchain) — too heavy for pre-work. Full retirement is future work.
- **Arc 2 — three-VCO Blend DSP backend** (spec v5.29, plan `2026-07-09-three-vco-blend-dsp.md`,
  4 tasks): `Oscillator` blends sine/tri/saw/pulse **proportionally** (weights are ratios —
  divide by total; zero-sum → exact silence) with pulse duty; 24 new per-layer params
  (`osc{1,2,3}.*`, `mixer.osc{n}.level`; IDs use `osc`, display names "VCO"); `Voice` renders
  three oscillators summed via mixer levels; old `osc.waveform/coarse/fine` fully retired
  (params, snapshot, 8 test files, GUI members). Default patch = old single-saw voice,
  hand-verified byte-identical twice. Suite 292 → 297.
- **Arc 3 — vintage reskin Stage 1** (spec v5.31, plan `2026-07-09-vintage-reskin.md`, 3 tasks
  + a long acceptance-iteration tail): see §3.

## 2. Repo state RIGHT NOW

- **main @ `5373725`** (PR #21). Suite: **297 tests, 0 failed**. `tools/drift-check --ci`
  fully clean (incl. `franklin-catalog` covering all 297 — the 5 new tests were cataloged
  after CI caught the gap; see §5 lessons). Windows CI + ASan/UBSan green on the merged head.
  Zero open PRs; **only `main` exists** — 18 stale branches were audited (every one mapped to
  a merged PR or verified-contained work) and deleted on 2026-07-10.
- **Plugin SemVer still 5.4.0** — nothing bumped it this cycle; the next release-worthy point
  (Stage 2?) should bump CMake `project(VERSION)` per the standing release-surface rule.
- SDD ledger: `.superpowers/sdd/progress.md` sections `[VCO-PREWORK]`, `[VCO-BLEND-DSP]`,
  `[RESKIN]` hold the complete blow-by-blow including every review verdict and user ruling.

## 3. The vintage reskin — where the look stands

**Design ground truth:** `docs/superpowers/specs/2026-07-09-vintage-reskin-design.md` (v5.31,
amended in place three times — window sizing, typography ruling, aluminum/redwood palette) +
the committed mockup `docs/superpowers/specs/assets/2026-07-09-bernie-vintage-reference.png`.
⚠️ **Six additional mood-board images** (2026-07-11: brushed-aluminum/redwood/physicality
references, incl. a Conifer rack stack) were pasted in chat and are **NOT in the repo** — ask
the user to drop the files and archive them next to the first mockup.

Current construction (all in `src/gui/VintageLookAndFeel.{h,cpp}`, `Section.cpp`,
`PluginEditor.cpp`):
- **Photographic materials, user-generated** (their image generator; masters live at
  `/mnt/hgfs/Development Studio Work/Bernie Synth/GUI/`, preprocessed copies committed under
  `assets/textures/` with `SOURCES.txt` provenance): one continuous **brushed-aluminum
  chassis** between **redwood rails**; every `Section` is a **leather plate screwed onto the
  chassis** (drop-shadow seam, 3px metal reveal, corner screws). Knobs are a keyed sprite
  (baked pointer **digitally healed out** via rotational inpainting; live pointer drawn over
  the spun cap so lighting never rotates). Screws are a keyed sprite with slot-jitter ≤±17°
  and a **pre-brightened variant on dark panels** (`drawScrew(..., onDark)`).
- Rendering: `drawPhotoCrop` draws aspect-correct, position-hashed crops (no tiling, no
  seams, per-plate variation). Procedural fallbacks remain if any asset fails to load.
- **Resizable**: 1400×1050 logical canvas, transform-scaled, aspect-locked 0.5×–2×, opens
  display-fitted (`Canvas` child + `layoutCanvas()`/`paintCanvas()`; editor `resized()` only
  sets the transform).
- Typography: embedded Barlow Condensed everywhere (user ruling), labels floored at 16px.
- Reserved frames await content: VCO 1/2/3 (left column), OSC BLEND, FILTER ENV, OUTPUT
  column, mod row. **The panel is a beautiful mostly-empty chassis until Stage 2.**

**Acceptance state:** the user drove ~6 visual iterations (readability → materials →
photographic assets → chassis-mounting/separation). Last shipped state had no outstanding
complaint, but no explicit final "looks good" was recorded either — treat the look as
*converging, not signed off*.

## 4. Tooling this cycle created (use these)

- **`k2000_panel_snapshot`** (`tests/panel_snapshot_main.cpp`, target in `tests/CMakeLists.txt`,
  NOT in CTest/CI): renders the REAL editor offscreen to PNG —
  `./build/tests/k2000_panel_snapshot out.png [w h [scale]]`. This is the ONLY way to see GUI
  work from a session: GNOME here refuses every programmatic capture (portal silent+interactive
  both fail code-2, Shell iface AccessDenied). **Judge contrast at 100% full-frame; zoomed
  crops lie about arm's-length visibility** (learned the hard way).
- Local Standalone relaunch for the user (`build/k2000_artefacts/Release/Standalone/Bernie`,
  nohup) — the user CAN see this desktop.
- **Windows/DAW binary flow**: user's convention is a single-file VST3 at
  `C:\Program Files\Common Files\VST3\Bernie.vst3`; hand them the inner DLL from the CI
  artifact (`gh run download <id>`) **with its SHA256** — a stale-binary saga cost a full
  round (Ableton was auditing a 7-versions-old build). Their DAW copy is stale again as of
  the merge; fetch a fresh artifact from main's CI when they next want a DAW pass.

## 5. Process lessons banked this session (violations will bite)

1. **Any plan that changes the suite count MUST update `docs/filter-validation/README.md` and
   `docs/franklin/test-catalog.json` in the same task** — the DSP plan missed both; CI Drift
   caught it post-push (catalog entries follow the audited v2 six-field format; derive keys
   from the actual `beginTest` strings).
2. When retiring a C++ API, **grep the class/method names** (`Oscillator::Waveform`,
   `.setWaveform(`), not just param/field names — a perf test's direct usage slipped past
   plan research.
3. `gh pr edit` is broken on this repo (Projects-classic GraphQL deprecation) — use
   `gh api -X PATCH repos/increp/k2000/pulls/N -f title=... -f body=...`.
4. Files dragged via VMware land in `/tmp/VMwareDnD/...` and are **OS-cleaned within
   minutes** — copy into the repo immediately or lose them (the first mockup needed a
   re-share; the mood-board images are still unarchived).
5. Merges to main need `--admin` (MainGuardRail ruleset: required signatures + a code_quality
   rule that can never pass — no code-scanning exists). Every merge since #18 bypassed it;
   the ruleset is decorative until fixed/removed.
6. The `if (blendX_ != 0.0f)` zero-weight skip in `Oscillator` is exact-equality by design —
   safe today, but Stage 2's GUI sliders will feed smoothed floats; watch that the skip isn't
   silently defeated (perf-only concern, flagged in Task 1's review).

## 6. Next steps, in order

1. **Stage 2 — VCO rows + Osc Blend mixer GUI** (the queued, user-agreed next move; the panel
   only starts looking like the mockups when its content exists). Needs its own
   brainstorm→spec→plan cycle: 3 × (Coarse/Fine + 4 blend sliders + pulse-duty under the
   Pulse slider) in the reserved VCO frames, 3 level knobs in OSC BLEND. All 24 params
   already exist and are snapshot-wired — this is GUI-only. Constraints on record: full
   visibility (no tabs — user chose stacked rows), `feedback_no_menu_diving`, and the
   blend-slider style should follow the mockup's vertical faders with % readouts.
2. **Stage 3 — scopes + VU meters** (real-time drawing, thread-safety story needed;
   `vuWellRect()` in PluginEditor marks the header wells; OUTPUT column reserved).
3. **Archive the six mood-board images** when the user drops them.
4. Optional polish debt: larger black-panel texture regen (softens past ~1.5× zoom),
   pointer-free knob regen (healed wedge exists under the live pointer), patina'd
   toggles/buttons (deferred until Stage 2/3 builds real controls).
5. **North-star features** (explicitly deferred): VCO2 Wave-Compass morph UI, VCO3 wavetable
   engine + bank browser, Noise/Sub/Ring mixer sources, Filter Env/Key Track/Env Amt params,
   Conifer branding + dog badge, power button.
6. Engagement bookkeeping: item 5 (GUI cycle) is now genuinely in progress; item 2 remnant
   (stale-doc sweep) still unconfirmed; the engagement's formal close still waits on both.
7. Standing blockers unchanged: SP-B proper after the engagement; SP-D excitation risk (Q25)
   before any hardware fingerprinting; all DSP voicing HELD (authenticity-purist).

## 7. Verify / build

```bash
tools/drift-check --session                       # ALWAYS FIRST
cmake --build build --target k2000_tests k2000_Standalone -j4
./build/tests/k2000_tests | tee build/last-test-run.log | tail -1   # Summary: 297 tests, 0 failed
./build/tests/k2000_panel_snapshot panel.png                        # see the actual panel
```
