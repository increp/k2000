# Vintage Panel Reskin (GUI Stage 1 of 3) — Design

**Version:** 5.31 (artifact) · **Date:** 2026-07-09 · **Status:** Approved in brainstorm; build follows
**Reference image:** `assets/2026-07-09-bernie-vintage-reference.png` (user-provided mockup, 1448×1086;
the visual ground truth for all three GUI stages)
**Depends on:** `2026-07-09-three-vco-blend-design.md` (shipped — the DSP backend this GUI eventually fronts)

---

## 1. Problem and vision

The user provided a complete visual mockup of Bernie as a vintage hardware instrument: cream
textured-metal header/footer, walnut side rails, corner screws, charcoal module panels, chunky
black knobs with inset value readouts, analog VU meters, per-VCO wave-preview scopes. Today's
panel is the flat dark "Summit" look. The mockup also shows future *feature* territory
(differentiated VCO engines, wavetables, extra mixer sources) that is explicitly **not** being
built now.

**User ruling (2026-07-09): "the look, now — features later."** The GUI work is decomposed into
three sequenced sub-projects, each its own spec → plan → build cycle:

1. **THIS SPEC — Aesthetic foundation:** full-panel reskin + reference geography. No new params.
2. **VCO section + Mixer build-out:** the three "Wave Recipe" rows (blend sliders, pitch, duty)
   and the Mixer's three VCO levels — the GUI for the already-shipped three-VCO DSP backend.
3. **Scopes & meters:** per-VCO wave previews, header VU meters, output meters. Real-time drawing
   components with their own thread-safety story.

**Deferred north-star roadmap** (from the mockup, explicitly not scheduled): VCO2 "Wave Compass"
single-knob morph UI, VCO3 true-wavetable engine + bank browser, Noise/Sub-Osc/Ring-Mod mixer
sources, Filter Env / Key Track / Env Amt / per-env Velocity params, branding (name/logo TBD).

## 2. Decisions (user rulings, 2026-07-09 brainstorm)

| Decision | Ruling |
|---|---|
| Layout scope | **Adopt the reference geography** — sections move to match the mockup, not repaint-in-place |
| Branding | **None for now** — blank reserved header zone; wordmark/logo added later |
| Window size | ~~1400×1050 fixed~~ **AMENDED 2026-07-10** (user ruling at Ableton acceptance: fixed size overflowed a DPI-scaled 4K screen): the panel is a **1400×1050 logical canvas, transform-scaled** to an aspect-locked resizable window (limits 0.5×–2×); opens fitted to the display work area |
| Rendering | **Hybrid**: procedural vector structure + startup-cached grain textures + one embedded OFL condensed font |
| Spine accent | **Re-express as brass trim** — thin warm-brass label-strip underline on constant-Summit-spine sections replaces the blue-violet `spineEdge` border (amends the earlier visual-language decision; concept survives, vocabulary changes) |
| Power button | Skipped (decorative in the mockup) |
| Master gain | Relocated from top-bar horizontal slider to a header **OUTPUT** rotary knob (top right, per mockup) |

## 3. Palette & VintageLookAndFeel

New `src/gui/VintageLookAndFeel.{h,cpp}` **replaces** `SummitLookAndFeel` outright (one look for
the whole editor; no dual-skin state; the old files are deleted — no back-compat constraint).

Palette (sampled from the reference, named constants like today's):

| constant | approx | role |
|---|---|---|
| `windowBg` | #18171A | area behind the module panels |
| `creamPanel` | #D6D0C2 | header/footer/rail base plate |
| `creamText` | #2A2620 | dark text on cream |
| `charcoalPanel` | #222124 | module panel fill |
| `charcoalWell` | #1A191C | recessed wells (value boxes, scope frames) |
| `panelEdge` | #141316 | module borders / engraved lines |
| `woodRail` | #6B4A32 | side-rail base |
| `capText` | #E2E0DA | caps labels on charcoal |
| `dimText` | #8D8A82 | reserved/dimmed |
| `brassTrim` | #B79B5E | spine-section label underline (the re-expressed accent) |
| `amberLed` | #E8A13C | indicator accents |
| `ledRed` | #D8452C | LIMIT light when active |

Rendering:
- **Knobs:** black body with subtle radial sheen, thin metallic rim, white pointer line, fine
  tick ring around the sweep, dark inset value box beneath (the readout style seen under every
  reference knob). Replaces the current flat arc style in `drawRotarySlider`.
- **Textures:** generated once at startup into cached `juce::Image`s — fine speckle grain for
  cream plates, low-contrast striation for wood rails — then tiled/blitted in paint. Generator
  isolated in `VintageLookAndFeel` so photographic PNGs can replace it later without touching
  callers. No per-frame procedural drawing.
- **Typography:** one OFL-licensed condensed sans (Barlow Condensed) embedded via BinaryData,
  used for **all** label text — captions, titles, and numeric value readouts alike (user
  ruling at final review, 2026-07-09: condensed everywhere; the mockup's own value boxes read
  condensed, and legibility is re-checked in the live-tuning pass).
- **Combos/toggles:** dark inset fields matching the value-box style; existing compact-combo
  fixes (arrow zone, font size) carried over.

## 4. Chrome

- **Header (~90 px, cream, full width):** left = blank reserved branding zone; small version
  label (`Bernie vX.Y.Z` from `JucePlugin_VersionString` — keeps the version-surface rule);
  center-right = two recessed blank VU plates (Stage 3 fills them); right = Edit Layer combo +
  oversampling hamburger (restyled, same functions) + **OUTPUT** knob (master gain, 0–10 style
  scale ticks).
- **Wood rails:** ~14 px full-height striped-grain rails left and right.
- **Footer (cream):** the Layer Routing strip — Enable, Key Lo/Hi, Vel Lo/Hi, Level, Channel —
  exactly today's controls, restyled (the mockup's footer is literally this section).
- **Screws:** drawn corner screws on module panels (radial-gradient disc + slot line).

## 5. Geography (1400×1050)

- **Left column (~52% width): three stacked VCO panels** — "VCO 1", "VCO 2", "VCO 3" labeled
  recessed frames, **deliberately empty** (Stage 2 fills them). Three equal rows (our engine has
  three identical blend VCOs; the mockup's differentiated engines are north-star).
- **Center-right: tall VCF panel** — everything today's Filter section holds: model combo,
  cutoff (visually dominant, per mockup), resonance, slope, Huggett routing/separation/post-drive
  ↔ Moog mode (existing visibility switching), HP pre-filter band. Plus a reserved
  **FILTER ENV** sub-frame (future mod work; params don't exist yet).
- **Far right: slim OUTPUT column** — reserved frame for Stage 3's stereo meter.
- **Bottom control row:** **OSC BLEND** panel (reserved — Stage 2's mixer levels live here),
  **VAST DSP** panel (the Algo combo's new home; hidden Drive/Mix stay hidden), **AMP ENV**
  (A/D/S/R knobs), **AMP** (Safety toggle, Limiter, LIMIT light).
- **Mod row:** MOD ENVS | LFO 1–4 | MOD MATRIX | FX CHAINS — reserved frames, restyled.
- Reference-mockup params that don't exist yet (Key Track, Env Amt, per-env Velocity) simply
  don't appear — no dead controls.

## 6. Code structure

- Create: `src/gui/VintageLookAndFeel.h/.cpp` (palette, texture cache, all control rendering),
  font asset + BinaryData wiring in CMake.
- Modify: `src/gui/Section.h/.cpp` (recessed vintage panel, screws, label strip, `spine` flag →
  brass underline instead of blue border), `src/PluginEditor.h/.cpp` (`setSize(1400,1050)`,
  `resized()` rewritten for §5's geography, master-gain slider→knob, header/footer/rails
  painting), `src/gui/LabeledKnob` (value-box styling hooks if needed).
- Delete: `src/gui/SummitLookAndFeel.h/.cpp` (fully superseded).
- **Untouched:** all params, DSP, bindings, processor code. This is paint + layout only.

## 7. Non-goals

- No new params or DSP (nothing binds that isn't already bound).
- No VCO-row controls, no mixer levels (Stage 2), no scopes/meters (Stage 3).
- No branding assets. No resizable UI. No preset/back-compat concerns (standing decision).
- No attempt at per-pixel mockup fidelity — proportions and vocabulary, tuned live against the
  running Standalone (per the standing "eyes on the real panel" rule).

## 8. Verification

Suite stays green with zero test changes expected (`cmake --build build --target k2000_tests
-j4 && ./build/tests/k2000_tests`). **Correction from plan-writing research:** the suite never
compiles or instantiates the editor — `createEditor()` returns nullptr under `K2000_TESTING`
and no GUI sources are in the test target — so the suite guards only against non-GUI fallout;
compile-correctness of the GUI comes from building the plugin targets. Both `k2000_tests` and
`k2000_Standalone` build clean. **Acceptance is visual:** the user eyeballs the running
Standalone (this environment cannot screenshot — known XWayland/portal limitation) checking:
whole panel reads as the reference's vintage hardware look; all existing controls present and
operable in their new geography; VCO/OSC-BLEND/OUTPUT/FILTER-ENV frames present and visibly
reserved; version label correct; Moog↔Huggett switching still swaps the right controls.
Windows CI smoke into Ableton remains the trusted pre-merge gate for the branch.
