# v5 — Constant Summit voice (selectable spine filter)

**Status:** Design proposed, 2026-06-16.

**Scope:** The v5 keystone — build the always-present Summit spine as a **selectable, live-switchable `FilterModel` library** with click-free hot-swap, shipping **Huggett** (dual TPT SVF + separation, gray-box) as the flagship default model, promoted out of the optional palette into the constant spine slot — **stereo, per-Layer**, at the 256-voice target. **Decomposition:** v5 is large, so it splits into two specs — **this one is the filter + model-selection architecture**; a **companion v5 spec** covers the rest of the spine (the VCA + amp/mod envelopes, LFOs, mod matrix, voice modes). Moog is a v5.1 fast-follow (one more library entry) and is out of scope here except where the abstraction must accommodate it.

**Context:** Implements roadmap phase v5 and its [deep-dive](../roadmap/phases.md#v5-deep-dive--the-selectable-summit-spine). Locks the spine-filter architecture (register **L7**) and resolves **Q12–Q16**; raises **Q17–Q19**. The reframe from "the Huggett filter" to "a selectable filter model, Huggett default" was approved 2026-06-16 (this supersedes the earlier plan to land Moog as a v7 graph block).

**Sources:** [huggett-filter.md](../architecture/huggett-filter.md) and the [Moog/Huggett research brief](../architecture/moog-ladder.md); JUCE `dsp::StateVariableTPTFilter`; the existing polymorphic-block idiom ([ADR-0002](../decisions/0002-polymorphic-dsp-slots.md)) and stable-ID param namespace ([ADR-0008](../decisions/0008-algorithm-selection-and-param-namespace.md)).

## Thesis

**The spine is always a real analog filter voice — and which filter is itself a live, automatable choice.** The "Summit" identity lives in the always-on drive → VCA → modulation and in the **default** (Huggett), not in forbidding other characters. Selecting a model — even mid-performance, even automated — never clicks, because each voice crossfades between two real filter instances rather than reconfiguring one.

## Architecture (locked: L7)

```
  per Layer:  spine.filterModel  (automatable choice; stable IDs)
              spine.{cutoff,resonance,drive,output}   ← common core
              spine.<modelId>.<param> …               ← per-model banks

  per Voice:  SpineFilterSlot
              ┌──────────────────────────────────────────────┐
              │  active   : FilterModel  (in-place, heap-free) │
              │  outgoing : FilterModel  (in-place, heap-free) │ during a switch
              │  xfade    : equal-power ramp  → frees outgoing │
              └──────────────────────────────────────────────┘
                         ▲ setCommon(cutoff,res,drive) every block
                         ▲ model-specific setters from the active bank
```

### Components

**`FilterModel`** — the abstract per-voice filter. RT-safe, **heap-free**, stereo. One model instance per voice (×2 transiently during a switch). Interface (sketch):

```cpp
class FilterModel {
public:
    virtual ~FilterModel() = default;
    virtual void prepare(double sampleRate) noexcept = 0;  // cheap; no heap
    virtual void reset() noexcept = 0;
    virtual void setCommon(float cutoffHz, float resonance, float drive) noexcept = 0;
    virtual void processBlock(float* left, float* right, int numSamples) noexcept = 0;
    // model-specific params are set through the concrete type via the model's bank
};
```

`prepare()` must not allocate — models are heap-free value types so construction + `prepare` are RT-safe in place. Model-specific parameters are applied by the owning slot through the concrete type (the library knows each model's bank).

**`FilterModelLibrary`** — append-only registry, **stable integer IDs** (the `AlgorithmLibrary` pattern). Each entry: id, display name (UTF-8 via `util::u8`), in-place factory, and a param-bank descriptor. Adding Oberheim later = append one entry + one class; existing presets/automation are untouched (Q18 governs the size budget).

**`SpineFilterSlot`** (per voice) — owns two fixed-size, in-place model storages sized to the **largest** registered model, the active/outgoing pointers, and the crossfade ramp. Drives `setCommon` + the active model's bank each block, runs the model(s), applies the equal-power crossfade during a switch.

**Common core** — `spine.cutoff/resonance/drive/output`: always on the front panel, always mod-matrix destinations, carried across model switches unchanged. The common `drive` is the model's **pre-filter input drive** (every model has one, fed via `setCommon`); model-specific extras like Huggett's **post-filter drive** live in that model's bank.

## Live hot-swap (Q17)

On a change of the (per-Layer, automatable) `spine.filterModel`, the Layer flags it; each active voice's `SpineFilterSlot`:

1. **In-place constructs** the incoming model into its spare storage (no heap, no audio-thread allocation).
2. Runs **both** models for an **equal-power crossfade** over a short fixed ramp (duration TBD, Q17).
3. **Retires** the outgoing model when the ramp completes.

Notes started after the change use only the new model (no fade). Cost: a **transient ~2× filter** across the Layer's active voices during the fade. Guards: ignore no-op reselect; debounce / cap concurrent fades against rapid selector automation (Q17). The selector is a **stepped** choice param (Q19).

## Parameter & modulation model (resolved: param-model crux)

Static APVTS (a curated library keeps the count bounded): the **common core** plus **per-model namespaced banks** `spine.<modelId>.<param>` (the [ADR-0008](../decisions/0008-algorithm-selection-and-param-namespace.md) idiom). The UI shows only the active model's extended cluster (the dynamic knob-cluster plan); the mod matrix addresses the **core** stably regardless of model, and a model's bank params are mod-targetable while that model is active. Serialization stores the model id + core + all banks, so presets are stable across model switches and across appended models.

## The Huggett model at v5 (resolved: Q13/Q14/Q15)

- **Q13 — dual from v5.** Two linked 12 dB **TPT state-variable** cells + a **separation** offset; *not* a ladder, *not* direct-form biquads. Internal milestones de-risk: linear single cell → dual + separation → nonlinear → calibrate.
- **Q14 — Summit dual modes first.** LP/BP/HP, 12/24 dB (cascade), series/parallel dual combinations with filter-frequency separation. OSCar separation-law modes are a marked **stretch**.
- **Q15 — gray-box.** Three tanh-class nonlinear stages: pre-filter **input drive**, a **resonance-loop saturator** (self-oscillation self-limits instead of blowing up), and **post-filter drive**. White-box OTA modeling is future work, no committed phase.
- Controls: cutoff (exp ≈16 Hz–20 kHz, multiplicative smoothing), resonance (tapered toward self-oscillation), separation, slope, input drive, post drive, key-track, output trim. Per-parameter smoothing.

## Performance (resolved: Q12)

L7's heap-free/in-place rule **forbids a per-voice `juce::dsp::Oversampling` object**. Policy: the linear TPT core runs un-oversampled; the **nonlinear drive stages** get **cheap, fixed per-voice anti-aliasing, conditional on drive being engaged**. The exact factor/scheme is chosen **behind the perf gate** (Q11), profiled at 256 voices × full stereo — measured, not assumed. Denormals off; SIMD-friendly per-voice buffers.

## Preset migration (resolved: Q16)

A cumulative shim migrates v1–v4 presets onto the spine with `spine.filterModel = Huggett`; the old optional `SVFFilter` block's cutoff/resonance map onto the **common core**; per-model banks default. A migration test guards the mapping.

## Testing

- **Linear-reference** — TPT cell frequency/phase response, 12/24 dB slopes, separation offset; deterministic.
- **Musical-behavior** — drive/resonance/separation interaction, self-oscillation onset constant across pitch/sample-rate.
- **Hot-swap** — switching is click-free (bounded output discontinuity), allocates no heap on the audio thread, and frees the outgoing model after the ramp; no-op reselect is ignored.
- **Migration** — v1–v4 preset → Huggett spine mapping.

## Scope boundaries

- **In (v5):** `FilterModel`/`FilterModelLibrary`/`SpineFilterSlot`, the param model, live crossfade hot-swap, **Huggett (dual, gray-box)**, the permanent Summit front panel for the spine, preset migration.
- **v5.1:** **Moog ladder** as the second library entry (staged linear-ZDF → nonlinear → cheap AA) — proves extensibility.
- **Out (later):** Oberheim+ models; OSCar separation modes; white-box OTA; any per-voice oversampling object; mod-matrix on the model selector itself (Q19 to confirm whether it is a destination).

## Open questions raised

- **Q17** crossfade duration + selector-automation debounce / concurrent-fade cap.
- **Q18** per-voice slot size budget governance as models are appended.
- **Q19** stepped-automation semantics of the live model selector; whether it is itself a mod destination.
