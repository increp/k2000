# Hearing-Safety Output Limiter — Design

**Version:** 5.04 (artifact; distinct from plugin SemVer)
**Date:** 2026-06-21
**Status:** Approved (brainstorm) — pending spec review
**Roadmap item:** `v5-safety-limiter` (user priority; scheduled after the Cmajor spike).

---

## 1. Purpose & premise

Extreme resonance and Separation settings (and the spine's self-oscillating filters) can launch a signal tens of dB hotter than the music — a real hazard to hearing and equipment. This adds a **global output safety limiter** as the last stage before audio leaves the plugin: it **guarantees no output sample exceeds a fixed ceiling**, is **ON by default**, and can be **defeated only by a deliberate, warned operator action** — never silently by a preset, a recalled project, or automation.

A plugin can only guarantee it never *outputs* above full scale; actual room loudness is the listener's monitor volume. So "hearing-safety" concretely means: **a hard, inviolable output ceiling.**

---

## 2. Decisions (resolved in brainstorm, 2026-06-21)

- **Mechanism → smooth limiter + hard-clip backstop.** A fast, zero-latency peak limiter (instant attack, short release) rides everyday peaks down to the ceiling smoothly; a final hard clip at the ceiling makes exceeding it mathematically impossible (catches any float-edge overshoot). Zero added latency (no lookahead).
- **Defeat control → protected, non-automatable.** The enable is **not** an APVTS parameter — automation and host parameter recall cannot touch it. It defaults **ON** for every fresh instance and is changed only by the warned UI toggle. Its state persists in the plugin's saved XML state; **absent → ON** (old projects and fresh instances are protected).
- **Ceiling → fixed −12 dBFS, tunable.** A single `// CALIB` constant (`kSafetyCeilingDb = -12.0f`), retunable after smoke-testing. **Note:** at −12 dBFS (with the −9 dB master default) the limiter is *gently active during normal loud playing* — it is a loudness governor, not only a rare emergency net — so it must sound good when continuously engaged. The user may raise the ceiling later.
- **Hand-rolled DSP.** A small `SafetyLimiter` class, header-only, matching the project's hand-rolled DSP convention (`NlSvfCell`, `AsymSaturator`) rather than `juce::dsp::Limiter`.
- **Stereo-linked.** One gain-reduction figure applied to both channels (no image shift).
- **Indicator in v1.** A UI indicator lights when the limiter is actively reducing gain.

---

## 3. Architecture & files

```
src/dsp/SafetyLimiter.h            NEW — header-only stereo-linked limiter + clip backstop
src/PluginProcessor.h/.cpp         MODIFY — own + prepare the limiter; protected enable; apply in
                                   processBlock after master gain; persist enable in XML state;
                                   publish gain-reduction; accessors for the editor
src/gui/PluginEditor.{h,cpp}       MODIFY — Safety Limiter toggle + modal disable warning + GR indicator
tests/SafetyLimiterTests.cpp       NEW — DSP guarantees (ceiling, no overshoot, passthrough, stereo-link, release)
tests/CMakeLists.txt               MODIFY — register the test
```
(Enable-persistence is covered by extending the existing `PluginLifecycleTests` — `PluginProcessor` is already in the test target.)

### 3.1 `SafetyLimiter` (header-only)

```cpp
class SafetyLimiter {
public:
    void prepare(double sampleRate) noexcept;   // compute release coefficient
    void reset() noexcept;                        // gain_=1, gr_=0
    // In-place, stereo-linked. right may be nullptr (mono). n samples.
    void process(float* left, float* right, int n) noexcept;
    float gainReductionDb() const noexcept;       // last block's max GR (>=0 dB), for the UI
private:
    static constexpr float kSafetyCeilingDb = -12.0f;  // CALIB — tune after smoke-test
    static constexpr float kReleaseMs       = 80.0f;   // CALIB
    double sampleRate_ = 48000.0;
    float  ceilingLin_ = /* dB->lin(-12) */;
    float  relCoeff_   = 0.0f;   // exp release one-pole coeff
    float  gain_       = 1.0f;   // current gain-reduction multiplier (<=1)
    float  grMaxDb_    = 0.0f;   // max GR this block (for the indicator)
};
```

**Per-sample algorithm** (instant attack, exponential release):
```
peak  = max(|left[i]|, right ? |right[i]| : 0)
target = (peak > ceilingLin_) ? (ceilingLin_ / peak) : 1.0f   // <=1
// instant attack: clamp down immediately; release: ease back up
gain_  = (target < gain_) ? target : (target + (gain_ - target) * relCoeff_)
l = left[i] * gain_;  (r = right[i] * gain_ if stereo)
// hard-clip backstop — inviolable ceiling regardless of gain_ path
left[i]  = clamp(l, -ceilingLin_, +ceilingLin_)
right[i] = clamp(r, -ceilingLin_, +ceilingLin_)   // if stereo
grMaxDb_ = max(grMaxDb_, -20*log10(gain_))
```
- `relCoeff_ = exp(-1 / (kReleaseMs * 0.001 * sampleRate_))`.
- Instant attack applying to the *same* sample → the limiter alone already prevents overshoot on the detected sample; the clip is a numerical backstop.
- `reset()` zeroes `grMaxDb_`; `process` resets `grMaxDb_` to 0 at block start, updates it per sample, leaves it readable until the next block.

### 3.2 Processor wiring

- Members: `SafetyLimiter limiter_;`, `bool limiterEnabled_ = true;`, `std::atomic<float> gainReductionDb_{0.0f};`.
- `prepareToPlay`: `limiter_.prepare(sampleRate); limiter_.reset();`.
- End of `processBlock` (after the master-gain loop): if `limiterEnabled_`, call `limiter_.process(L, R, n)` on the output buffer (L = ch0; R = ch1 if `outCh > 1` else nullptr), then `gainReductionDb_.store(limiter_.gainReductionDb())`. If disabled, store 0.
- `getStateInformation`: write `root->setAttribute("limiterEnabled", limiterEnabled_ ? 1 : 0)`.
- `setStateInformation`: `limiterEnabled_ = root->getBoolAttribute("limiterEnabled", true)` (absent → true).
- Public: `bool isLimiterEnabled() const; void setLimiterEnabled(bool); float gainReductionDb() const { return gainReductionDb_.load(); }`.

### 3.3 UI (non-test build only; `createEditor` returns nullptr under `K2000_TESTING`)

- A **Safety Limiter** toggle in the master/output cluster, reflecting `isLimiterEnabled()` (lit when ON).
- On a user click that would turn it **OFF**: show a modal warning (`juce::NativeMessageBox::showOkCancelBox` or an `AlertWindow`): *"Disabling the safety limiter allows dangerously loud output that can damage hearing or equipment. Continue?"* — **Cancel** reverts the toggle; **Disable** calls `setLimiterEnabled(false)`.
- Turning it **ON** is immediate (no warning).
- A **gain-reduction indicator** (small lamp/label) lit when `processor.gainReductionDb() > ~0.1 dB`, polled on the editor timer.

---

## 4. Testing

`tests/SafetyLimiterTests.cpp` (JUCE `UnitTest`, test target):
- **Ceiling enforced:** a 4×-over-ceiling sine → every output sample `|x| <= ceilingLin + 1e-6`.
- **No overshoot on transient:** silence then a single huge sample → that sample's output `<= ceilingLin` (instant attack + clip).
- **Below-ceiling passthrough:** a sine at half the ceiling → output bit-identical to input (gain stays 1, no clip).
- **Stereo-link:** loud L + quiet R → both scaled by the same factor (R/L amplitude ratio preserved within tolerance; no independent R limiting).
- **Release recovers:** after a loud burst returns to quiet, `gain_` (observed via output level on a steady quiet tone) returns toward unity within ~release time.
- **GR reporting:** `gainReductionDb()` is 0 for a below-ceiling signal and > 0 for an over-ceiling signal.

`tests/PluginLifecycleTests.cpp` (extend): enable **defaults ON**; `getStateInformation`→`setStateInformation` round-trips both ON and OFF; state with **no** `limiterEnabled` attribute loads as ON.

Perf is not a concern (a handful of ops/sample at the output). All gates are hard asserts. Build `-j4`; run `./build/tests/k2000_tests`.

---

## 5. Out of scope (YAGNI)

- Lookahead / true-peak (inter-sample) limiting, oversampled limiting.
- A user-adjustable ceiling control (fixed `// CALIB` constant for now).
- Per-voice or per-layer limiting (this is one global output stage).
- Metering beyond the single "limiting active" indicator (no GR meter/history).
- Making the enable an automatable parameter or exposing it to a future preset system as defeatable.

---

## 6. Success criteria

Output is provably incapable of exceeding the ceiling (tests pass on monstrous input); the limiter is ON by default and survives save/reload; it can be disabled only via the warned UI action and never by automation/recall; below-ceiling audio is unaffected; the indicator shows when it engages. A clean smoke-test at −12 dBFS (then retune the constant if desired) closes the feature.
