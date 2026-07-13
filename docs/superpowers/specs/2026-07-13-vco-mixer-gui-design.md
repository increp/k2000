# VCO Rows + Osc Blend Mixer (GUI Stage 2 of 3) — Design

**Version:** 5.33 (artifact) · **Date:** 2026-07-13 · **Status:** Approved in brainstorm; plan follows
**Amended 2026-07-13 (plan-writing):** §5/§6 — one suite test added (`ValueFormatTests`, the §8
round-trip requirement needs a real home), suite 297 → 298; the original "tests untouched" line
contradicted §8 and lost.
**Reference image:** `assets/2026-07-09-bernie-vintage-reference.png` (visual ground truth; VCO 1's
"Wave Recipe" row is the direct template — our three rows are identical instances of it)
**Depends on:** `2026-07-09-vintage-reskin-design.md` (Stage 1 — the chassis and reserved frames this
stage fills) · `2026-07-09-three-vco-blend-design.md` §7 (the approved GUI intentions this stage builds)

---

## 1. Problem and goal

Stage 1 shipped the vintage chassis with three deliberately-empty VCO plates and a reserved
OSC BLEND panel. The three-VCO Blend DSP backend is live: 24 per-layer params
(`osc{1,2,3}.coarse/.fine/.blend.{sine,triangle,saw,pulse}/.blend.pulseDuty`,
`mixer.osc{1,2,3}.level`) exist, are snapshot-wired, and are audible — but only from a host's
generic parameter list. Stage 2 gives them their panel: the three Wave Recipe rows and the
mixer's three level knobs. **GUI-only: no param, DSP, processor, or snapshot changes.**

This completes the three-VCO feature's user-facing surface → plugin SemVer bumps
**5.4.0 → 5.5.0** (CMake `project(VERSION)`; the panel label derives from
`JucePlugin_VersionString` automatically, honoring the release-surface rule).

## 2. Decisions

New rulings (user, 2026-07-13 brainstorm):

| Decision | Ruling |
|---|---|
| Stage-3 scope slots | **Reserve a blank recessed WAVE PREVIEW well in each VCO row now** — Stage 3 drops scopes in without moving any control (the `Section.h` "placeholder holds its layout slot" convention, same as the header VU plates) |
| Value readouts | **Fix every value box on the panel**, not just the new controls — one formatting pass to instrument-style text with units (§4); typing values back with units parses |
| Code structure | **Composite `VcoRow` component** (owns one row's controls + well; editor holds three) over flat ×3 members or struct+loops |

Inherited rulings (recorded in the depends-on specs; not re-opened):
three stacked identical rows, full visibility, no tabs · each row = Coarse, Fine, four Blend
faders (Sine/Triangle/Saw/Pulse), Pulse Duty positioned under the Pulse fader · blend faders are
vertical with % readouts (mockup style) · mixer = three level knobs in the OSC BLEND panel ·
labels say "VCO 1/2/3" · no dead controls (no Drift/Shape/Noise/Sub-Osc/Ring-Mod — params don't
exist) · exact pixel sizing is tuned live against snapshots/Standalone, not pre-specified.

## 3. Panel design

### 3.1 VCO rows (the three left-column plates, ~686×179 logical each)

Each row flips from reserved-dimmed to live and contains, inside `contentBounds()`:

- **Left ~60% — Wave Recipe.** Four vertical faders **SINE · TRI · SAW · PULSE**: caption above,
  fader, dark inset **% readout** below (`0.48` renders `48%`). Blend weights are ratios
  (proportional blend), so percentages read naturally; defaults SAW 100%, others 0%.
  Under the PULSE fader's readout only: a compact horizontal **DUTY** slider
  (0.01–0.99 shown `1%`–`99%`; `50%` = square). The other three columns leave that strip empty
  so all four faders stay top- and bottom-aligned.
- **Right ~40%.** Top: the reserved **WAVE PREVIEW** recessed well (dimmed caption, blank until
  Stage 3). Bottom: **COARSE** (±24, snaps to integer semitones, reads `+7 st`) and **FINE**
  (±100, reads `-12 ct`) knobs.
- All three rows are pixel-identical by construction (one `VcoRow` class, §5).

### 3.2 OSC BLEND panel (bottom-left plate)

Flips from reserved to live: three `LabeledKnob`s **VCO 1 · VCO 2 · VCO 3** with % readouts,
bound to `mixer.osc{n}.level`. Defaults `100% / 0% / 0%` — a fresh patch keeps sounding like
the old single-saw voice. Panel title stays "Osc Blend".

## 4. Readout formatting (whole panel)

One shared formatter/parser home (§5, `ValueFormat`) applied as JUCE
`textFromValueFunction` / `valueFromTextFunction` pairs. Editing a value box accepts the same
units back (bare numbers assume the display unit). Per family:

| Controls | Format | Examples |
|---|---|---|
| Blend faders, DUTY, mixer levels | integer % | `48%`, `50%` |
| COARSE | signed integer + `st` | `+7 st`, `0 st` |
| FINE | signed integer + `ct` | `-12 ct` |
| Filter Cutoff | < 1 kHz: integer `Hz`; ≥ 1 kHz: 2-dec `kHz` | `250 Hz`, `2.50 kHz` |
| HP Cut | `Off` at 0 (bypass position), else as Cutoff | `Off`, `120 Hz` |
| Reso, HP Reso, Post Drv, Sep, Amp S | plain 2-decimals (Sep signed, unit `oct`) | `0.20`, `+1.50 oct`, `0.80` |
| Amp A/D/R | < 1 s: integer `ms`; ≥ 1 s: 2-dec `s` | `5 ms`, `1.20 s` |
| OUTPUT (master gain), footer Level | 1-decimal `dB` | `-9.0 dB`, `0.0 dB` |
| Key Lo/Hi, Vel Lo/Hi | integers | `0`, `127` |

Hidden controls (Drive/Mix, Moog Wave/Octave/Bass) stay hidden and are not formatted.
Exact precision/thresholds may be tuned in the live pass; the table is the intent.

## 5. Code structure

**New files:**

- `src/gui/LabeledFader.{h,cpp}` — caption label above a `LinearVertical` slider with
  `TextBoxBelow`. Sibling of `LabeledKnob`: owns **no** attachment; owner binds `slider()`
  through `ParamBinder`.
- `src/gui/VcoRow.{h,cpp}` — `class VcoRow : public Section`. Owns four `LabeledFader`s, the
  DUTY mini-slider + label, COARSE/FINE `LabeledKnob`s. `resized()` lays them out inside
  `contentBounds()`; `paint()` draws the Section plate, then the recessed WAVE PREVIEW well +
  dimmed caption. Exposes seven `juce::Slider&` accessors for binding
  (`sine() tri() saw() pulse() duty() coarse() fine()`) and `previewWellBounds()` for Stage 3.
- `src/gui/ValueFormat.{h,cpp}` — formatter/parser pairs (`pct`, `st`, `ct`, `hz`, `hzOff`,
  `plain(n)`, `octSigned`, `envTime`, `db`) applied to sliders; used by `VcoRow` and the editor.

**Modified:**

- `src/gui/VintageLookAndFeel.{h,cpp}` — add `drawLinearSlider`: recessed charcoal track +
  brushed-metal cap with grip line, vertical and horizontal variants (the one new piece of
  rendering; matches the mockup's fader vocabulary).
- `src/PluginEditor.{h,cpp}` — the three `Section vco{1,2,3}Section_` members become three
  `VcoRow`s (a `VcoRow` **is a** `Section`, so `layoutCanvas()` geography keeps the same three
  `setBounds` calls); `mixerSection_` gains three editor-owned `LabeledKnob`s and drops its
  reserved flag; `bindLayer()` binds each row's seven sliders + three mixer levels (a local
  table maps row index → that VCO's seven `LayerIds` fields); `buildStaticControls()` applies
  §4 formatting to the existing value boxes.
- `CMakeLists.txt` — new sources; `project(VERSION 5.5.0)`.

**Untouched:** DSP, params, processor, snapshot code. One suite addition (amended 2026-07-13):
`tests/ValueFormatTests.cpp` carries the §8 round-trip check — suite 297 → 298, with
`docs/filter-validation/README.md` and `docs/franklin/test-catalog.json` updated in the same
task (the banked drift lesson). No other test changes.

## 6. Verification & acceptance

- Build: `cmake --build build --target k2000_tests k2000_Standalone -j4`; suite **298/0**
  (tee to `build/last-test-run.log`; includes the new ValueFormat round-trip test). The suite
  never compiles the editor — GUI compile-correctness comes from the plugin targets.
- Visual: `./build/tests/k2000_panel_snapshot out.png`, judged **at 100% full-frame** (zoomed
  crops lie — Stage 1 lesson); then acceptance iterations against the relaunched local
  Standalone, exactly like the reskin tail.
- Functional (live panel): Edit Layer 0↔1 rebinds all 24 new controls (ParamBinder
  detach-before-rebind contract); Moog↔Huggett switching still swaps the right controls;
  typing `2.5 kHz` / `48%` into value boxes parses; fresh instance shows SAW 100%,
  mixer 100/0/0 and sounds identical to the old single-saw default.
- Pre-merge: Windows CI (`gh workflow run build.yml --ref <branch>`) → Ableton; hand over the
  inner `Bernie.vst3` DLL **with SHA256** (the user's DAW copy is stale since PR #21 — this
  pass doubles as the refresh).
- **Watch item (from the 2026-07-11 handoff):** the DSP's zero-weight render skip is
  exact-equality (`blendX_ != 0.0f`). Verify a fader parked at 0% delivers exact `0.0f` to
  `Oscillator` (no smoothing in the path). If anything smooths it, **flag — do not fix here**
  (that would be a DSP change; perf-only concern).

## 7. Non-goals

No scopes or meters (wells stay blank — Stage 3). No DSP or parameter changes. No morph macro.
No Noise/Sub-Osc/Ring-Mod mixer rows. No branding. Hidden Drive/Mix and Moog knobs stay hidden.
Preset backward-compat remains a non-constraint (standing decision). No register changes —
no new architecture-level questions arose (Q9's dynamic-panel problem concerns the future
variable source/DSP region, not these fixed spine-adjacent rows).

## 8. Risks / tuning notes

- Row height is the tight axis (~150 px content): caption + fader + readout + duty strip must
  breathe. Mitigations live in the acceptance pass (shrink duty strip, caption size 16 px floor
  per the typography ruling). Fader body target ≈ 90 px.
- `drawLinearSlider` must handle both orientations from day one (vertical faders, horizontal DUTY).
- Custom `valueFromTextFunction` must be inverse-consistent with the formatter or host-driven
  text edits will drift values; the plan should include a round-trip check per family.
- The editor's `masterGain_` text box is 64 px wide — `-60.0 dB` fits, but verify at 100% zoom.
