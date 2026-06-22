# Spine Model Hot-Swap (v5.1) — Design

**Version:** 5.05 (artifact; distinct from plugin SemVer)
**Date:** 2026-06-22
**Status:** Approved (brainstorm) — pending spec review
**Roadmap item:** `v5.1` — Q17 click-free model hot-swap (`tools/roadmap-dashboard/roadmap.json`).
**Resolves:** register Q17 (crossfade duration + automation debounce + Q17a/Q17b hazards), Q18 (per-voice slot size budget).
**Unblocks:** ADR-0012 follow-up — Cmajor adapter state heap→in-place; the first production Cmajor block (Moog, v5.2).

---

## 1. Purpose & premise

The spine is a **selectable `FilterModel` library** (ADR-0011, register L7). Today only one model (Huggett) is registered, so switching is latent. v5.1 builds the machinery to change the spine filter **model** — including across model *types* — **while notes are sounding, without a click**, and removes the last heap dependency from the per-voice spine state so the L7 "heap-free, in-place" invariant holds before a second model (Moog) ever ships.

Two intertwined goals:

1. **Heap → in-place per-voice state.** Each voice's filter state currently lives in a heap box (`std::unique_ptr<FilterModel::State>` via `makeState()`). Live model switching can't safely allocate/free on the audio thread, so the state moves into fixed, voice-owned, in-place storage sized to the largest registered model.
2. **Click-free live model switch.** When the selected model changes on a *sounding* voice, the voice runs both the old and new models in parallel for a short, adjustable window and **equal-power crossfades** old → new.

A second model is required to exercise the crossfade. Per the roadmap, the first *production* second model is Moog (v5.2); v5.1 therefore registers a **test-target-only** second model (the existing pattern — `CmajorSvfFilter` is already test-only) to drive the crossfade tests end-to-end.

---

## 2. Decisions (resolved in brainstorm, 2026-06-22)

- **Architecture → Approach A.** Model-declared in-place state lifecycle + `Layer` pre-builds one instance of every registered model + the crossfade engine lives in `SpineFilterSlot`. (Rejected: `std::variant` state — bakes the model list into a compile-time type, breaks the runtime registry and the Cmajor adapter pattern. Rejected: keep heap state with two `unique_ptr`s — violates L7, allocates on the audio thread, doesn't unblock Cmajor.)
- **Model lifetime → pre-build all.** `Layer` builds one instance of every registered `FilterModel` at `prepare()` (tiny config+vtable objects, no per-voice state). A switch only changes `currentModelId_` — **zero audio-thread allocation**, and it removes the existing `make_unique`-on-the-audio-thread wart at `Layer.cpp:36`. Models are never freed during operation, so the Q17a "freed before per-voice state retired" dangle is **structurally impossible**.
- **Crossfade → per-voice, equal-power.** The spine is per-voice (each note has independent filter history), so the fade is per-voice: two in-place state buffers per voice, both models run in parallel, mixed `gNew·new + gOld·old` with `gOld² + gNew² = 1`.
- **Fade time → adjustable, global, automatable.** A single global APVTS parameter `spine.modelFadeMs`, range **2–100 ms**, default **25 ms** (`// CALIB` default), persists in state, automatable. The 2 ms floor keeps every switch click-free; lower = snappier with more click risk (documented). Captured at **fade-begin** — a mid-fade automation move applies to the *next* switch, never re-scales one in flight.
- **Rapid automation → coalesce, depth-1.** At most one fade in flight per voice. A new selection arriving mid-fade is remembered as a single `pending_` target (overwritten by the newest); when the current fade completes, one more fade runs to `pending_`. No-op reselect (same id) is ignored.
- **Note-start bind (Q17b) → snap, no fade.** A fresh or stolen voice binds to its Layer's current model on note-on. If the model type differs from the buffer's current state, the state is re-constructed in place; **no fade** — the amp envelope starts at 0, so there is nothing to click. This closes the cross-layer voice-steal hazard (`VoiceManager.cpp:40` re-`setLayer`s but never re-prepares the spine).
- **Q18 size budget → compile-time governed.** A `kMaxSpineStateBytes` constant with a per-model `static_assert`. Exceeding it fails the build; a reviewer then bumps the cap (recompile; costs `2 × Δ × kNumVoices` RAM) or slims/rejects the model. A runtime test also asserts every registered model fits.
- **HP pre-stage → in-place, no fade.** The non-swappable HP pre-stage state also moves heap → in-place for consistency, but needs no dual-buffer/fade treatment.

---

## 3. Architecture & files

### 3.1 `FilterModel` interface — `src/dsp/spine/FilterModel.h`

Replace heap `makeState()` with an in-place lifecycle. `State` keeps its virtual destructor, so the base provides a default `destroyState`:

```cpp
class FilterModel {
public:
    struct State { virtual ~State() = default; };
    virtual ~FilterModel() = default;
    virtual void prepare(double sampleRate) noexcept = 0;

    // In-place state lifecycle. constructState placement-news into caller memory
    // (caller guarantees >= stateSize() bytes at >= stateAlign() alignment).
    // INVARIANT: heap-free and RT-safe — callable on the audio thread during a live switch.
    virtual std::size_t stateSize()  const noexcept = 0;
    virtual std::size_t stateAlign() const noexcept = 0;
    virtual State* constructState(void* mem) const = 0;          // placement-new; no heap
    virtual void   destroyState(State* s) const noexcept { if (s) s->~State(); }  // virtual-dtor dispatch

    virtual void reset(State& s) const noexcept = 0;
    virtual void setCommon(float cutoffHz, float resonance, float drive) noexcept = 0;
    virtual void processStereo(State& s, float* left, float* right, int numSamples) const noexcept = 0;
};
```

Concrete models implement `stateSize`/`stateAlign`/`constructState`; `makeState()` is removed. Example (`HuggettFilter`):

```cpp
std::size_t stateSize()  const noexcept override { return sizeof(VoiceState); }
std::size_t stateAlign() const noexcept override { return alignof(VoiceState); }
State* constructState(void* mem) const override {
    auto* vs = new (mem) VoiceState();
    vs->a.prepare(sampleRate_); vs->b.prepare(sampleRate_); vs->dc.prepare(sampleRate_);
    return vs;
}
```

Same change applies to `HuggettHpStage` (in-place state) and `CmajorSvfFilter` (test-only).

### 3.2 Storage constants — new `src/dsp/spine/SpineState.h` (or in `SpineFilterSlot.h`)

```cpp
constexpr std::size_t kMaxSpineStateBytes = /* measured largest VoiceState + headroom, pinned in plan */;  // GOVERNED — Q18
constexpr std::size_t kSpineStateAlign    = alignof(std::max_align_t);
constexpr float       kDefaultModelFadeMs = 25.0f;  // CALIB (default for spine.modelFadeMs)
constexpr float       kMinModelFadeMs     = 2.0f;   // floor — keeps switches click-free
constexpr float       kMaxModelFadeMs     = 100.0f;
static_assert(sizeof(HuggettFilter::VoiceState) <= kMaxSpineStateBytes, "Huggett state exceeds spine slot");
// one static_assert per registered model
```

### 3.3 `SpineFilterSlot` — `src/dsp/spine/SpineFilterSlot.{h,cpp}`

Dual in-place state buffers + fade engine. No `unique_ptr`; nothing heap-allocated after `prepare`.

```cpp
class SpineFilterSlot {
public:
    void prepare(double sampleRate, int maxBlockSize,
                 const FilterModel* model, const HuggettHpStage* hp);   // constructs initial state
    void bind(const FilterModel* model, const HuggettHpStage* hp) noexcept; // note-start; snap, no fade (Q17b)
    void reset(const FilterModel* model, const HuggettHpStage* hp) noexcept;
    void processStereo(const HuggettHpStage* hp, bool hpEnabled,
                       const FilterModel* current, float fadeMs,
                       float* left, float* right, int numSamples) noexcept;
private:
    alignas(kSpineStateAlign) std::byte buf_[2][kMaxSpineStateBytes];
    FilterModel::State* state_[2] = {nullptr, nullptr};
    const FilterModel*  model_[2] = {nullptr, nullptr};   // model each buffer's state belongs to
    int   active_   = 0;            // live / fade-source buffer index
    int   fadePos_  = 0;            // 0 = steady; 1..fadeLen_ = samples into the current fade
    int   fadeLen_  = 0;            // captured at fade-begin from fadeMs
    const FilterModel* pending_ = nullptr;   // coalesce depth-1
    double sampleRate_ = 44100.0;
    std::vector<float> scratchL_, scratchR_; // sized at prepare; touched only mid-fade
    // HP in-place state buffer (single; no fade)
    alignas(kSpineStateAlign) std::byte hpBuf_[/* sized to HuggettHpStage::State */];
    HuggettHpStage::State* hpState_ = nullptr;
};
```

### 3.4 `Layer` — `src/Layer.{h,cpp}`

- Replace the single `spineModel_` with an array holding one pre-built instance of every registered model, all `prepare()`d. `currentModelId_` selects the active one.
- `updateParameters`: set `currentModelId_` from the snapshot (no allocation); push the **common core** (`setCommon`) to **all** pre-built models so the outgoing model keeps tracking cutoff/res/drive during a fade; push Huggett-specific banks (routing/slope/separation/postDrive) to the Huggett instance via the existing `huggett_` view.
- Expose `spineModel()` (current) and `spineModel(std::size_t id)`.

### 3.5 `Voice` / `VoiceManager` — `src/Voice.cpp`, `src/VoiceManager.cpp`

- `Voice::prepare` → `spine_.prepare(sr, maxBlock, layer model, hp)`.
- `Voice::noteOn` → `spine_.bind(layer's current model, hp)` (snap to the Layer's model; Q17b).
- `Voice::render` → pass the live `spine.modelFadeMs` into `spine_.processStereo(...)`.

### 3.6 Parameter — `src/params/Parameters.{h,cpp}`, `ParamSnapshot`

- New **global** (non-per-Layer) automatable float `spine.modelFadeMs`, range 2–100 ms, default 25, carried on `ParamSnapshot` so it reaches the render path. (Exact APVTS wiring deferred to the plan; follow the existing global-vs-per-Layer param pattern.)

### 3.7 Test-only second model — `src/dsp/spine/cmajor/` or `tests/`

Register a test-target-only second `FilterModel` with **construct/destroy counters** (the crossfade-test fixture). The existing `CmajorSvfFilter` (already test-only) is a candidate; a purpose-built counting double may be clearer for leak/lifecycle assertions.

---

## 4. Control & data flow

`SpineFilterSlot::processStereo(current, fadeMs, l, r, n)`:

1. **HP first, once.** If `hpEnabled`, run the HP pre-stage in-place on `(l,r)`. Its output feeds the model path(s).
2. **Switch detection** (`current != model_[active_]`):
   - **Begin** (if `fadePos_ == 0`): `other = 1 - active_`; `destroyState(state_[other])` if stale; `state_[other] = current->constructState(buf_[other])`; `model_[other] = current`; `fadeLen_ = clamp(round(fadeMs · sr / 1000), 1, …)`; `fadePos_ = 1`; `pending_ = nullptr`.
   - **Coalesce** (if already fading and `current != model_[other]`): `pending_ = current`.
3. **Process:**
   - **Steady** (`fadePos_ == 0`): `model_[active_]->processStereo(state_[active_], l, r, n)`.
   - **Fading:** copy `(l,r) → (scratchL,scratchR)`; run **new** model in place on `(l,r)`, **old** model in place on the scratch copy; per sample with `p = min(fadePos_/fadeLen_, 1)`, `gOld = cos(p·π/2)`, `gNew = sin(p·π/2)`: `out = gNew·new + gOld·old`; advance `fadePos_`.
4. **Completion** (block end, never mid-block): if `fadePos_ ≥ fadeLen_` → `destroyState(state_[active_])`; `active_ = other`; `fadePos_ = 0`. If `pending_` set and `pending_ != model_[active_]`, the **next** block begins the queued fade (else clear `pending_`).

`SpineFilterSlot::bind(model)` (note-start, Q17b): if `model_[active_] != model`, `destroyState(state_[active_])`, `state_[active_] = model->constructState(buf_[active_])`, `model_[active_] = model`; clear `fadePos_`/`pending_`. No fade.

---

## 5. Crossfade math

Equal-power (constant-power) crossfade. With normalized progress `p ∈ [0,1]`:

```
gOld(p) = cos(p · π/2)
gNew(p) = sin(p · π/2)
gOld² + gNew² = 1          // constant power through the blend (no mid-fade dip)
out      = gNew · newOut + gOld · oldOut
```

`fadeLen_ = round(fadeMs · sampleRate / 1000)`, captured at fade-begin and held constant for that fade. `p` clamps at 1 so a fade completing partway through a block holds full-new for the remainder.

---

## 6. Q18 — per-voice slot size governance

- `kMaxSpineStateBytes` is sized to the **largest** registered model's `VoiceState` plus headroom; pinned from a real measurement in the plan.
- A `static_assert` per registered model fails the build if it exceeds the cap.
- **Process on overflow:** reviewer bumps the cap (recompile; RAM cost `2 × Δ × kNumVoices`, ×`kNumLayers` is not multiplied — slots are per-voice) or slims/rejects the model.
- A runtime test iterates `FilterModelLibrary` and asserts `stateSize() ≤ kMaxSpineStateBytes` for every registered model — catches a model added without its own `static_assert`.

---

## 7. Testing strategy

Test fixture: a test-target-only second `FilterModel` with construct/destroy counters.

- **In-place migration (pure refactor):** existing Huggett tests stay green with **bit-identical** output; one new test asserts the in-place `constructState`/`processStereo` path matches the prior heap path sample-for-sample.
- **Crossfade:**
  - *Click-free:* sample-to-sample Δ across a live switch stays below a bounded threshold (no discontinuity).
  - *Equal-power:* the `gOld²+gNew²=1` gain law holds across the fade; a steady-tone blend holds ~constant power (within tolerance).
  - *Duration:* a fade completes in ~`round(fadeMs·sr/1000)` samples; varying `fadeMs` scales it; the 2 ms floor and 100 ms ceiling clamp.
  - *Coalesce depth-1:* A→B then mid-fade→C ends on C; at most one fade live at any instant; old states destroyed exactly once (counter proves no leak/double-free).
- **Q17b cross-layer steal:** two layers with different model types; force a steal across layers (exhaust the voice pool); assert the stolen voice binds clean (no fade) and outputs finite/correct samples.
- **Q18:** compile-time `static_assert` present; runtime test asserts every registered model fits.
- **Lifecycle/no-leak:** many rapid switches leave construct count == destroy count + live states.

---

## 8. Build order (TDD)

1. **In-place state migration** (`FilterModel` interface, Huggett, HP stage, `SpineFilterSlot` single in-place buffer) — output-identical refactor; existing tests stay green. Add `kMaxSpineStateBytes` + `static_assert`s (Q18).
2. **`Layer` pre-builds all models; alloc-free id switch** — removes the audio-thread `make_unique`.
3. **Register the test-only second model** (counting double).
4. **Crossfade engine + coalesce** in `SpineFilterSlot` (dual buffers, equal-power mix, `pending_`).
5. **`spine.modelFadeMs` parameter** wired through `ParamSnapshot` → render path.
6. **Q17b note-start bind** in the `Voice`/`VoiceManager` path.

---

## 9. Risks & open items

- **`constructState` RT-safety per model.** The invariant (heap-free, bounded) holds for Huggett and the Cmajor lean adapters; every future model must honor it. Stated on the interface; not separately enforceable at compile time.
- **2× spine CPU during a fade.** A fade runs both models per voice for ≤100 ms. At 256 voices this is a transient spike, not steady-state; acceptable within the v6 perf gate but worth a sanity check at the upper voice count.
- **Per-model bank dispatch is not generalized** in v5.1 (Huggett keeps its `huggett_` view; the test model only needs `setCommon`). Generalizing namespaced per-model banks lands with Moog (v5.2).
- **`kMaxSpineStateBytes` exact value** pinned in the plan from a measured `sizeof`, not guessed here.
