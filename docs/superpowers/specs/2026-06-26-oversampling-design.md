# Per-Voice Oversampling (HQ Tiers, Live/Render) — Design

**Version:** 5.09 (artifact; distinct from plugin SemVer)
**Date:** 2026-06-26
**Status:** Approved (brainstorm) — pending spec review
**Roadmap item:** `v5.3` — HQ oversampling tiers.
**Branch:** `feat/oversampling` (off `main`; assumes the `feat/engine-cleanup-decouple` checkpoint — DSP-layer decouple — has landed).
**Builds on:** the engine cleanup/decouple checkpoint (`20b394c`): DSP layer is now JUCE-free and portable, which is what makes a per-voice, no-JUCE oversampler clean to slot in.

---

## 1. Purpose & premise

Add user-selectable oversampling to cut aliasing from the synth's nonlinear stages, so the filters reach **final DSP quality before** the deferred reference-measurement/calibration phase (we calibrate the real, oversampled filter — not a version we're about to change).

Two facts from the codebase scope the work:
- **The oscillator is already band-limited** — Saw/Square/Triangle use polyBLEP, Sine is pure (`Oscillator.cpp`). It is *not* a meaningful aliasing source; oversampling it would waste CPU.
- The aliasing sources are the **nonlinear stages**: the graph **Waveshaper** block and the **spine** (HP cell + Huggett/Moog filter + the `tanh` drive/resonance saturators, worst at high drive and self-oscillation).

So oversampling wraps the nonlinear per-voice path only.

**Explicit non-constraint:** preset/patch backward-compatibility is NOT required (`[[feedback-no-preset-backcompat]]`).

## 2. Scope (what is oversampled)

**Whole per-voice nonlinear path.** Per voice, `Voice::render` splits into three rate zones:

```
[base sr]  osc (polyBLEP, clean)
   │  UP ×N
[N×sr]     graph Waveshaper (mono)  →  duplicate mono→stereo  →  spine (HP + filter model + tanh drives, stereo)
   │  DOWN ×N
[base sr]  amp envelope · velocity · level · spineOutput  →  accumulate into out L/R
```

- Osc and amp envelope stay at **base rate**.
- Everything in the oversampled domain (graph blocks, HP cell, Huggett, Moog Cmajor core) is **prepared at `N×sr`**, not base rate.
- `N` ∈ {1 (Off), 2, 4, 8}.

## 3. The oversampler (hand-rolled, portable)

A small, JUCE-free polyphase halfband, kept in the (now portable) DSP layer.

**`Halfband2x`** — a single 2× stage: linear-phase symmetric FIR halfband with `upsample(in[n]) → out[2n]` and `downsample(in[2n]) → out[n]`. Linear phase ⇒ a fixed, known group delay we report as latency. FIR (not IIR) is chosen deliberately: cleanliness is the whole point, and the latency is sub-millisecond and PDC-compensated.

**Factor by cascade:** 2× = one stage, 4× = two, 8× = three. Group delay accumulates across stages and is the reported latency.

**Design target:** ≈ 0.1 dB passband ripple and ≥ 80 dB image rejection in the stopband. Tap count is chosen to meet this; latency follows from the tap count. These numbers are the concrete pass/fail thresholds for the §9 unit tests.

**`VoiceOversampler`** — per voice; owns the up-stages (mono, for osc→graph) and the down-stages (stereo, for the spine output). Internal buffers are **pre-allocated for the maximum factor (8×)** at `prepareToPlay`, so a runtime factor change never allocates on the audio thread. The active depth (how many cascade stages engage) is switched, not reallocated.

**Interfaces (what each unit does / how it's used / what it depends on):**
- `Halfband2x`: value type, plain C++; `prepare()` clears state, `upsample`/`downsample` process; depends on nothing but `<array>`/`<cmath>`.
- `VoiceOversampler`: owns N `Halfband2x` stages + scratch; `prepare(maxBlock)` sizes for 8×; `setFactor(n)` selects active depth (non-RT — see §6); `processUp`/`processDown` bridge the rate zones; depends only on `Halfband2x`.

## 4. Per-voice integration

`Voice` gains a `VoiceOversampler`. `Voice::prepare(sr, maxBlock)` prepares the oversampled-domain DSP (graph blocks + spine + models) at the **active** `N×sr` and sizes the oversampler for 8×. `Voice::render` becomes: osc at base → up → graph (mono, N×) → duplicate to stereo → spine (stereo, N×) → down → envelope/level/output at base.

The mono→stereo split stays where it is today (after the graph, before the spine): one mono upsampler feeds the graph; the duplicated stereo signal feeds the spine; two mono (or one stereo) downsamplers return to base before the envelope.

## 5. Factor model & storage

Two **protected** settings (like `limiterEnabled` — NOT APVTS, NOT host-automatable), persisted in the plugin's instance state blob:
- `realtimeOS` ∈ {Off, 2×, 4×, 8×}
- `offlineOS` ∈ {Same as Realtime, 2×, 4×, 8×}  *(no "Off": export never drops below live quality)*

**Active factor** = `offlineOS` (resolving "Same as Realtime" to `realtimeOS`) when `isNonRealtime()`, else `realtimeOS`. This is the standard DAW pattern: light live, heavy on bounce.

Persistence: stored in the `K2000Root` state element alongside `limiterEnabled` (the cleanup left a minimal wrapper there). Round-trips like the limiter.

## 6. Factor switching (the non-trivial part)

Because the inner DSP is *tuned* for `N×sr`, **changing N is effectively an inner sample-rate change** → a re-prepare that recomputes coefficients across 64 voices and re-`initialise`s the Moog Cmajor core. That is **not RT-safe**.

So any factor change — a menu action, or the Live↔Offline transition — goes through:
1. `suspendProcessing(true)` (JUCE's sanctioned non-RT reconfig gate),
2. re-prepare all voices' oversampled-domain DSP at the new `N×sr`,
3. `setLatencySamples(newLatency)` (0 when Off; else the cascade group delay in base samples),
4. `suspendProcessing(false)`.

Factor changes are rare user actions; a brief reset is the accepted trade. **Live↔Offline transition:** not all hosts re-`prepareToPlay` for offline render, so `processBlock` detects an `isNonRealtime()` change and triggers the same re-prepare path defensively.

## 7. Moog max-frequency fix (root-cause, scripted)

The Moog ladder algorithm has **no rate ceiling** — it computes coefficients from `processor.frequency` at runtime, exactly like Huggett computes from `sampleRate_`. The 192 kHz limit is purely a **Cmajor codegen default**: the generated wrapper emits `getMaxFrequency(){return 192000.0;}` and `initialise()` throws above it (`generated/MoogLadder.h:612-619`). Verified: that constant is used **only** by the throw-guard, and the Moog has **no rate-proportional buffers**.

`cmaj generate` exposes **no max-frequency flag** (verified against `cmaj help`: codegen options are `--target`, `--output`, `--maxFramesPerBlock`, `--name`, plugin paths, `--iterations`). So we raise the cap at the source via a **deterministic post-codegen patch in `cmaj-codegen.sh`**: after `cmaj generate`, a scripted `sed` rewrites the generated max-frequency constant to **1,536,000 Hz (= 8 × 192k)**.

Rationale:
- **Reproducible** — lives in the codegen script; re-applied on every regen (not a vanishing manual edit).
- **Safe** — changes only the guard threshold; state size is unchanged (no rate-sized buffers), so `MoogLadderAdapter`'s `kGenBytes=512` and `FilterModelLibrary`'s `kMaxSpineStateBytes` static-asserts still hold.
- **Numerically sound** — ladder coefficients are continuous in `sr`; 384k/768k run fine.
- **Cap value 1,536,000** makes *every* factor×sample-rate combo valid, so the oversampler needs **zero** Moog-specific factor clamping; the guard still catches genuinely absurd input.

The `MoogLadderAdapter::prepare` clamp (`sr <= maxF`) stays as a belt-and-suspenders guard but will no longer trigger in normal use.

## 8. UI (per the Pulsar Echorec reference)

A **hamburger (⋮) button, top-right** of the editor → `juce::PopupMenu`:
- One entry **"Oversampling: Nx"** (N = current realtime factor) with a **submenu**.
- Submenu has two ticked groups (`addSectionHeader` + ticked `addItem`):
  - **Realtime oversampling:** Off / 2× / 4× / 8×
  - **Offline oversampling:** Same as Realtime / 2× / 4× / 8×
- A checkmark marks the active choice in each group.
- Menu callbacks (message thread) call `processor.setRealtimeOS()` / `setOfflineOS()`, which run the suspend→re-prepare→latency path (§6).
- The hamburger is extensible (About/version later); for this spec it carries only Oversampling.

## 9. Testing

- **`Halfband2x` unit tests:** passband flatness (gain ≈ 1, low ripple below Nyquist/2) and image rejection (a sine swept past Nyquist/2 must come back attenuated by the design stopband).
- **Anti-aliasing integration test:** drive Huggett and Moog hard into self-oscillation, FFT the output, assert aliased (inharmonic) energy drops materially at 4× vs 1×.
- **Factor-switch test:** changing factor re-prepares without crashing and reports the expected `setLatencySamples`; no allocation on the audio thread after `prepareToPlay` (the 8× pre-alloc holds).
- **Settings round-trip:** `realtimeOS`/`offlineOS` persist through save/load (mirrors the limiter round-trip in `PluginLifecycleTests`).
- **Moog cap regression:** after the codegen patch, assert `getMaxFrequency()` reflects the raised value and the state-size static-asserts still compile.

## 10. Risks

- **CPU:** whole-path 8× live × 64 voices over the Moog Cmajor ladder is heavy — that's exactly why "Off" exists for the realtime tier. Architecture supports it; performance headroom is the user's dial.
- **Live↔Offline host behaviour:** host re-prepare on offline render is inconsistent; mitigated by the defensive `isNonRealtime()` detection in `processBlock` (§6).
- **Latency reporting timing:** changing latency outside `prepareToPlay` is allowed but handled variably by hosts; for a synth the impact is benign (PDC aligns generated audio).
- **Codegen patch drift:** the `sed` target string must match future cmaj output; a `grep` assertion in `cmaj-codegen.sh` (fail if the expected constant isn't found) guards against silent no-op on a cmaj upgrade.

## 11. Non-goals (YAGNI)

- Oversampling the oscillator (already band-limited).
- Global/post-mix oversampling (does not remove per-voice nonlinear aliasing).
- Making oversampling automatable / per-preset (it's a protected quality setting).
- IIR/minimum-phase oversampling (FIR linear-phase chosen; revisit only if latency proves a problem).
- Moog-internal nested resampling (obviated by §7).

## 12. File-level change map

- **New:** `src/dsp/Halfband2x.h` (portable), `src/dsp/VoiceOversampler.{h,cpp}`; tests `tests/Halfband2xTests.cpp`, `tests/OversamplingAntiAliasTests.cpp`.
- **Modified:** `src/Voice.{h,cpp}` (oversampler member + render restructure + prepare at N×sr). Factor-threading path: `PluginProcessor` owns `realtimeOS`/`offlineOS`, computes the active factor, and passes it down through `Program::prepare` → `VoiceManager::prepare` → `Voice::prepare` (so the oversampled-domain DSP is prepared at N×sr). `src/PluginProcessor.{h,cpp}` (protected settings, persistence, suspend/re-prepare, `setLatencySamples`, `isNonRealtime` handling); `src/PluginEditor.{h,cpp}` (hamburger + PopupMenu).
- **Cmajor toolchain:** `tools/cmajor/cmaj-codegen.sh` (post-gen `sed` + `grep` guard); regenerate `src/dsp/spine/cmajor/generated/MoogLadder.h` with the raised cap.
- **Roadmap:** mark `v5.3 — HQ oversampling tiers` accordingly; bump `CMakeLists.txt` VERSION + panel label (`[[release-version-surface]]`).
