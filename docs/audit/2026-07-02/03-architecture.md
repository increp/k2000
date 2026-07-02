# Architecture — Layering, Coupling, Ownership, Thread Safety

**Version:** 5.12 · **Date:** 2026-07-02 · Part of the [codebase health audit](README.md).

## Include graph (scripted, full `src/`)

- **Header cycles: 0** (DFS over the resolved local-include graph).
- **Layer flow:** `top (Plugin*/Voice/Layer/Program) → {dsp, params, gui, util}` —
  all downward except **one upward edge**:
  `src/params/Parameters.h → src/LayerRouting.h` (P3). `LayerRouting` is a
  plain data struct; moving it to `src/params/` or a shared `src/core/` kills
  the only inversion.
- **JUCE leakage into DSP: none that matters.** `src/dsp/` includes no JUCE
  GUI/audio-processor module; only `juce_core` in `FilterModelLibrary.h`
  (display names) and `MoogLadderAdapter.cpp`. The gui layer touches
  `juce_audio_processors` only for APVTS attachments (`ParamBinder`) — correct.
- `params/Parameters.cpp → dsp/{AlgorithmLibrary, FilterModelLibrary}` exists
  to build choice lists from registry names. Acceptable coupling; if a third
  registry appears, consider a name-provider interface so params stops
  depending on dsp concretes.

## God-class check

| File | Lines | Verdict |
|---|---|---|
| `src/PluginEditor.cpp` | 440 | Largest, but structured (Sections, LabeledKnob, ParamBinder). **Extract per-section components before the GUI sub-project** — it will not survive the planned growth (mod matrix, FX, multipart) as one file. |
| `src/params/Parameters.cpp` | 257 | One long `createLayout()` — table-driven, readable. Fine. |
| `src/PluginProcessor.cpp` | 146 | Orchestration only. Model. |

## Ownership & lifetime

- **P0 (see [01-memory-safety.md](01-memory-safety.md)):** `SpineFilterSlot`
  caches non-owning `FilterModel*` whose owner (`Layer::models_`) recreates
  instances on every `prepare` — use-after-free on re-prepare.
- **P3:** `Layer::prepare` assumes `models_[0]` is Huggett
  (`dynamic_cast` on index 0) but finds Moog by scanning. If the registry
  order ever changes, `huggett_` silently becomes null and every
  Huggett-specific param stops applying — no error. Find both by scan (or by
  registry id).
- **Good pattern (keep):** editor's `ParamBinder binder_` declared last with a
  comment explaining it must die first (attachments reference sibling
  widgets). This is the discipline the slot pointers lack.
- `Voice::layer_` is non-owning but its owner (`Program::slots_`, a fixed
  `std::array`) never reallocates — safe by construction. Worth one comment.

## Audio-thread safety

**Steady-state `processBlock` path — clean:**
- `params::snapshot`/`routing` read only `getRawParameterValue(...)->load()`
  (atomic floats). No locks, no allocation, no ValueTree access.
- Cross-thread flags (`limiterEnabled_`, `gainReductionDb_`, OS factors) are
  `std::atomic` with relaxed ordering — appropriate for independent flags.
- `ScopedNoDenormals` present; scratch buffers pre-sized in `prepareToPlay`
  with a `jassert` (not a silent realloc) on oversize blocks — correct choice.

**Borderline (documented, accepted-with-eyes-open):**
- The Live↔Offline transition calls `reprepareForOS()` **from the audio
  thread** (PluginProcessor.cpp:41): `suspendProcessing` + full re-prepare
  **allocates on the RT thread**. Offline direction is fine (non-realtime by
  definition); the offline→live edge allocates once in a live callback. The
  in-code comment claims safety for the offline case only. Consider deferring
  the live-edge re-prepare to the message thread via an atomic request flag.
- `setLatencySamples` from the same path notifies the host from the audio
  thread; most hosts tolerate it, some log warnings. Same fix shape.
- `params::snapshot` does ~30 string-keyed map lookups per layer per block
  (P3 perf): cache `std::atomic<float>*` per param id once at construction
  (JUCE's documented pattern) — removes all lookup cost from the RT path.

**Editor↔processor:** editor polls processor atomics on a `juce::Timer`
(limiter indicator) and never touches DSP state directly. Correct.

## Separation verdicts (processor / editor / DSP / params / presets)

Clean five-way split. State (presets) is one XML root with protected
non-APVTS fields explicitly attributed; no back-compat machinery per the
standing product decision, and `setStateInformation` validates the root tag
before touching anything. The one structural debt is the editor file size
(above) and the `Section` placeholders (`reserved`) which are fine as
scaffolding but should become real components in the GUI sub-project.
