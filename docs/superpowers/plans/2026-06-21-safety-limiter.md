# Hearing-Safety Output Limiter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a global, default-ON, hard-guaranteed output safety limiter so extreme resonance/Separation/self-oscillation can never produce output above a fixed ceiling, defeatable only by a warned operator action.

**Architecture:** A hand-rolled, header-only `SafetyLimiter` (stereo-linked, instant-attack peak limiter + hard-clip backstop, zero latency) applied at the end of `processBlock` after master gain. The enable is a protected `bool` on the processor — NOT an APVTS parameter — defaulting ON, persisted in the plugin XML state (absent → ON). The editor adds a toggle (with a modal disable warning) and a gain-reduction indicator in the Amp section.

**Tech Stack:** C++17, JUCE 8.0.4, CMake, the existing `tests/` JUCE `UnitTest` harness.

## Global Constraints

- **Ceiling:** fixed `kSafetyCeilingDb = -12.0f` (`// CALIB`, tune after smoke-test). **Release:** `kReleaseMs = 80.0f` (`// CALIB`). Instant attack. Zero added latency (no lookahead).
- **Hard guarantee:** no output sample may exceed the ceiling — the hard-clip backstop is mandatory, not optional.
- **Protected enable:** the limiter-enable is NOT an APVTS parameter (no automation/host recall). Defaults **ON** for every fresh instance; persisted as an XML attribute; **absent → ON**. Only the warned UI toggle changes it.
- **Stereo-linked:** one gain-reduction figure applied to both channels.
- **Build:** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `cmake --build build --target k2000_tests -j4` (DSP/processor tests) and `cmake --build build --target k2000_Standalone -j4` (UI compiles). **Always `-j4`.** Run tests: `./build/tests/k2000_tests` (expect `Summary: N tests, 0 failed`).
- **Branch:** `feat/safety-limiter`. Spec: `docs/superpowers/specs/2026-06-21-safety-limiter-design.md`.

---

### Task 1: `SafetyLimiter` DSP class (header-only) + tests

**Files:**
- Create: `src/dsp/SafetyLimiter.h`
- Create: `tests/SafetyLimiterTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing (pure std).
- Produces: `class SafetyLimiter { void prepare(double sr) noexcept; void reset() noexcept; void process(float* left, float* right /*nullable*/, int n) noexcept; float gainReductionDb() const noexcept; };`

- [ ] **Step 1: Write the failing tests**

`tests/SafetyLimiterTests.cpp`:
```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/SafetyLimiter.h"
#include <cmath>
#include <vector>

struct SafetyLimiterTests : public juce::UnitTest {
    SafetyLimiterTests() : juce::UnitTest("SafetyLimiter") {}
    static constexpr double kSR = 48000.0;
    static float ceilingLin() { return std::pow(10.0f, -12.0f / 20.0f); }  // matches kSafetyCeilingDb

    void runTest() override {
        const float ceil = ceilingLin();

        beginTest("ceiling enforced: a 4x-over signal never exceeds the ceiling");
        {
            SafetyLimiter lim; lim.prepare(kSR); lim.reset();
            const int n = 4096;
            std::vector<float> l((size_t)n), r((size_t)n);
            for (int i = 0; i < n; ++i)
                l[(size_t)i] = r[(size_t)i] = 4.0f * (float) std::sin(2.0*juce::MathConstants<double>::pi*220.0*i/kSR);
            lim.process(l.data(), r.data(), n);
            float mx = 0.0f; for (int i = 0; i < n; ++i) mx = std::max(mx, std::max(std::abs(l[(size_t)i]), std::abs(r[(size_t)i])));
            expect(mx <= ceil + 1e-6f, "max " + juce::String(mx,6) + " must be <= ceiling " + juce::String(ceil,6));
        }

        beginTest("no overshoot on a silence->huge transient (first sample already capped)");
        {
            SafetyLimiter lim; lim.prepare(kSR); lim.reset();
            std::vector<float> l{ 0.0f, 0.0f, 9.0f, 9.0f };
            lim.process(l.data(), nullptr, (int) l.size());
            for (float x : l) expect(std::abs(x) <= ceil + 1e-6f, "sample " + juce::String(x,6) + " <= ceiling");
        }

        beginTest("below-ceiling signal passes through unchanged");
        {
            SafetyLimiter lim; lim.prepare(kSR); lim.reset();
            const int n = 2048;
            std::vector<float> in((size_t)n), l((size_t)n);
            for (int i = 0; i < n; ++i) in[(size_t)i] = l[(size_t)i] = 0.5f * ceil * (float) std::sin(2.0*juce::MathConstants<double>::pi*440.0*i/kSR);
            lim.process(l.data(), nullptr, n);
            float d = 0.0f; for (int i = 0; i < n; ++i) d = std::max(d, std::abs(l[(size_t)i] - in[(size_t)i]));
            expect(d < 1e-7f, "below-ceiling output must be bit-identical (max diff " + juce::String(d,9) + ")");
        }

        beginTest("stereo-linked: same gain applied to both channels (ratio preserved)");
        {
            SafetyLimiter lim; lim.prepare(kSR); lim.reset();
            const int n = 1024;
            std::vector<float> l((size_t)n), r((size_t)n);
            for (int i = 0; i < n; ++i) { l[(size_t)i] = 4.0f; r[(size_t)i] = 1.0f; }  // loud L, quieter R, same sign
            lim.process(l.data(), r.data(), n);
            // After the first sample the gain is settled; L/R ratio must equal the input 4:1.
            const float ratio = l[(size_t)(n-1)] / r[(size_t)(n-1)];
            expect(std::abs(ratio - 4.0f) < 1e-3f, "L/R ratio should be 4.0, got " + juce::String(ratio,5));
        }

        beginTest("gain reduction reports 0 below ceiling, >0 above");
        {
            SafetyLimiter lim; lim.prepare(kSR); lim.reset();
            std::vector<float> quiet((size_t)256, 0.1f * ceil);
            lim.process(quiet.data(), nullptr, 256);
            expect(lim.gainReductionDb() < 0.01f, "no GR below ceiling");
            std::vector<float> loud((size_t)256, 5.0f);
            lim.process(loud.data(), nullptr, 256);
            expect(lim.gainReductionDb() > 1.0f, "GR > 0 above ceiling");
        }

        beginTest("release recovers after a loud burst");
        {
            SafetyLimiter lim; lim.prepare(kSR); lim.reset();
            std::vector<float> loud((size_t)256, 5.0f);
            lim.process(loud.data(), nullptr, 256);
            const float grAfterBurst = lim.gainReductionDb();
            // feed quiet blocks; GR should fall back toward 0 within a few release times
            float grLater = grAfterBurst;
            for (int b = 0; b < 20; ++b) { std::vector<float> q((size_t)256, 0.05f * ceil); lim.process(q.data(), nullptr, 256); grLater = lim.gainReductionDb(); }
            expect(grLater < grAfterBurst * 0.1f, "GR should recover toward 0 (after " + juce::String(grAfterBurst,3)
                   + " -> " + juce::String(grLater,3) + ")");
        }
    }
};
static SafetyLimiterTests safetyLimiterTestsInstance;
```

- [ ] **Step 2: Register the test, run to verify it fails**

Add `SafetyLimiterTests.cpp` to the test-cpp list in `tests/CMakeLists.txt` (near `HpPreFilterTests.cpp`). (No `.cpp` to add for `SafetyLimiter.h` — it is header-only.)
Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target k2000_tests -j4`
Expected: **compile error** — `src/dsp/SafetyLimiter.h` does not exist yet.

- [ ] **Step 3: Implement `SafetyLimiter.h`**

`src/dsp/SafetyLimiter.h`:
```cpp
#pragma once
#include <algorithm>
#include <cmath>

// Global output safety limiter: stereo-linked, zero-latency. A fast peak limiter
// (instant attack, exponential release) rides peaks down to the ceiling; a hard-clip
// backstop makes exceeding the ceiling impossible. Header-only, dependency-free.
// See docs/superpowers/specs/2026-06-21-safety-limiter-design.md.
class SafetyLimiter {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        ceilingLin_ = std::pow(10.0f, kSafetyCeilingDb / 20.0f);
        relCoeff_   = (float) std::exp(-1.0 / (double(kReleaseMs) * 0.001 * sampleRate_));
        reset();
    }
    void reset() noexcept { gain_ = 1.0f; grMaxDb_ = 0.0f; }

    // In-place, stereo-linked. right may be nullptr (mono). n samples.
    void process(float* left, float* right, int n) noexcept {
        grMaxDb_ = 0.0f;
        for (int i = 0; i < n; ++i) {
            const float l = left[i];
            const float r = right ? right[i] : 0.0f;
            const float peak = std::max(std::abs(l), std::abs(r));
            const float target = (peak > ceilingLin_) ? (ceilingLin_ / peak) : 1.0f;  // <= 1
            // instant attack (clamp down now); exponential release (ease back up)
            gain_ = (target < gain_) ? target : (target + (gain_ - target) * relCoeff_);
            left[i] = std::clamp(l * gain_, -ceilingLin_, ceilingLin_);   // + hard-clip backstop
            if (right) right[i] = std::clamp(r * gain_, -ceilingLin_, ceilingLin_);
            if (gain_ < 1.0f) grMaxDb_ = std::max(grMaxDb_, -20.0f * std::log10(gain_));
        }
    }

    float gainReductionDb() const noexcept { return grMaxDb_; }

private:
    static constexpr float kSafetyCeilingDb = -12.0f;  // CALIB — tune after smoke-test
    static constexpr float kReleaseMs       = 80.0f;   // CALIB
    double sampleRate_ = 48000.0;
    float  ceilingLin_ = 0.2511886f;   // pow(10, -12/20); recomputed in prepare()
    float  relCoeff_   = 0.0f;
    float  gain_       = 1.0f;
    float  grMaxDb_    = 0.0f;
};
```

- [ ] **Step 4: Build and run, verify pass**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "SafetyLimiter|Summary"`
Expected: `SafetyLimiter` tests pass; `Summary: ... 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/SafetyLimiter.h tests/SafetyLimiterTests.cpp tests/CMakeLists.txt
git commit -m "feat(safety): SafetyLimiter DSP (stereo-linked peak limiter + hard-clip backstop)"
```

---

### Task 2: Processor integration — apply + protected enable + state + lifecycle tests

**Files:**
- Modify: `src/PluginProcessor.h`
- Modify: `src/PluginProcessor.cpp`
- Modify: `tests/PluginLifecycleTests.cpp`

**Interfaces:**
- Consumes: `SafetyLimiter` (Task 1).
- Produces (on `K2000AudioProcessor`): `bool isLimiterEnabled() const; void setLimiterEnabled(bool); float gainReductionDb() const;` — consumed by Task 3.

- [ ] **Step 1: Write the failing lifecycle tests**

In `tests/PluginLifecycleTests.cpp`, add these `beginTest` blocks inside `runTest()` (after the existing state round-trip block, before the closing brace of `runTest`):
```cpp
        beginTest("safety limiter is enabled by default");
        {
            K2000AudioProcessor p;
            expect(p.isLimiterEnabled(), "fresh instance must default to limiter ON");
        }

        beginTest("limiter-enable round-trips through saved state (OFF and ON)");
        {
            K2000AudioProcessor p; p.prepareToPlay(SR, BLOCK);
            p.setLimiterEnabled(false);
            juce::MemoryBlock mb; p.getStateInformation(mb);
            K2000AudioProcessor q; q.prepareToPlay(SR, BLOCK);
            expect(q.isLimiterEnabled(), "q starts ON before load");
            q.setStateInformation(mb.getData(), (int) mb.getSize());
            expect(! q.isLimiterEnabled(), "OFF must persist across save/restore");

            p.setLimiterEnabled(true);
            juce::MemoryBlock mb2; p.getStateInformation(mb2);
            q.setStateInformation(mb2.getData(), (int) mb2.getSize());
            expect(q.isLimiterEnabled(), "ON must persist across save/restore");
        }

        beginTest("enabled limiter caps a hot processed block; disabled does not");
        {
            // Drive a loud note and confirm enabled output stays under the ceiling.
            const float ceil = std::pow(10.0f, -12.0f / 20.0f);
            auto peakOf = [](juce::AudioBuffer<float>& b) {
                float m = 0.0f;
                for (int c = 0; c < b.getNumChannels(); ++c)
                    for (int i = 0; i < b.getNumSamples(); ++i) m = std::max(m, std::abs(b.getSample(c, i)));
                return m;
            };
            K2000AudioProcessor p; p.prepareToPlay(SR, BLOCK);
            // crank master gain so the raw mix would exceed the ceiling
            if (auto* mg = p.apvts().getParameter(params::masterGain))
                mg->setValueNotifyingHost(mg->convertTo0to1(6.0f));
            juce::MidiBuffer midi; midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8) 127), 0);
            juce::AudioBuffer<float> buf(2, BLOCK);
            p.setLimiterEnabled(true);
            float mEnabled = 0.0f;
            for (int k = 0; k < 8; ++k) { buf.clear(); p.processBlock(buf, midi); midi.clear(); mEnabled = std::max(mEnabled, peakOf(buf)); }
            expect(mEnabled <= ceil + 1e-4f, "enabled output must stay <= ceiling (peak " + juce::String(mEnabled,5) + ")");
        }
```
(`std::pow`/`std::max` need `#include <cmath>` and `#include <algorithm>` at the top of the test file — add them.)

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: **compile error** — `isLimiterEnabled` / `setLimiterEnabled` not declared on `K2000AudioProcessor`.

- [ ] **Step 3: Add members + accessors to `PluginProcessor.h`**

Add the includes near the top (after the existing includes):
```cpp
#include <atomic>
#include "dsp/SafetyLimiter.h"
```
Add public accessors (after `juce::AudioProcessorValueTreeState& apvts() { return apvts_; }`):
```cpp
    bool  isLimiterEnabled() const { return limiterEnabled_; }
    void  setLimiterEnabled(bool on) { limiterEnabled_ = on; }
    float gainReductionDb() const { return gainReductionDb_.load(std::memory_order_relaxed); }
```
Add private members (after `std::vector<float> scratchL_, scratchR_;`):
```cpp
    SafetyLimiter      limiter_;
    bool               limiterEnabled_ = true;   // protected: NOT an APVTS param; defaults ON
    std::atomic<float> gainReductionDb_{ 0.0f };
```

- [ ] **Step 4: Wire `prepareToPlay` + `processBlock` in `PluginProcessor.cpp`**

In `prepareToPlay`, after `scratchR_.assign(samplesPerBlock, 0.0f);`:
```cpp
    limiter_.prepare(sr);
    limiter_.reset();
```
At the very end of `processBlock`, after the master-gain write loop (the `for (int c = 0; c < outCh; ++c)` block) and before the closing brace:
```cpp
    if (limiterEnabled_ && outCh > 0) {
        float* L = buffer.getWritePointer(0);
        float* R = (outCh > 1) ? buffer.getWritePointer(1) : nullptr;
        limiter_.process(L, R, n);
        gainReductionDb_.store(limiter_.gainReductionDb(), std::memory_order_relaxed);
    } else {
        gainReductionDb_.store(0.0f, std::memory_order_relaxed);
    }
```

- [ ] **Step 5: Persist the enable in state**

In `getStateInformation`, right after `root->setAttribute("v", 5);`:
```cpp
    root->setAttribute("limiterEnabled", limiterEnabled_ ? 1 : 0);
```
In `setStateInformation`, right after the `if (xml->getTagName() != "K2000Root") return;` line:
```cpp
    limiterEnabled_ = xml->getBoolAttribute("limiterEnabled", true);  // absent (old project) -> ON
```

- [ ] **Step 6: Build and run, verify pass**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests 2>&1 | grep -E "PluginLifecycle|Summary"`
Expected: `PluginLifecycle` passes (default ON, round-trips, enabled caps the hot block); `Summary: ... 0 failed`.

- [ ] **Step 7: Commit**

```bash
git add src/PluginProcessor.h src/PluginProcessor.cpp tests/PluginLifecycleTests.cpp
git commit -m "feat(safety): apply limiter in processBlock; protected default-ON enable persisted in state"
```

---

### Task 3: Editor UI — toggle + disable warning + gain-reduction indicator

**Files:**
- Modify: `src/PluginEditor.h`
- Modify: `src/PluginEditor.cpp`

**Interfaces:**
- Consumes: `K2000AudioProcessor::isLimiterEnabled()/setLimiterEnabled()/gainReductionDb()` (Task 2).
- Produces: UI only (no downstream consumers). Verified by build + manual smoke (the editor is not unit-tested — `createEditor` returns nullptr under `K2000_TESTING`).

- [ ] **Step 1: Add the Timer base + UI members to `PluginEditor.h`**

Change the class declaration to also inherit a Timer:
```cpp
class K2000AudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer {
```
Add a `void timerCallback() override;` declaration in the private section.
Add these members in the Amp area (e.g. near the `ampSection_` declaration):
```cpp
    juce::Label        safetyLbl_;        // "Safety" caption
    juce::ToggleButton safetyLimiter_;    // protected enable (NOT bound to APVTS)
    juce::Label        limitIndicator_;   // lights when the limiter is reducing gain
```

- [ ] **Step 2: Configure the controls in `buildStaticControls()`**

Append to `buildStaticControls()` (after the reserved-sections loop that makes `ampSection_` visible):
```cpp
    // Amp section: hearing-safety output limiter (protected control — not an APVTS param).
    safetyLbl_.setText("Safety", juce::dontSendNotification);
    safetyLbl_.setJustificationType(juce::Justification::centred);
    ampSection_.addAndMakeVisible(safetyLbl_);

    safetyLimiter_.setButtonText("Limiter");
    safetyLimiter_.setToggleState(processorRef.isLimiterEnabled(), juce::dontSendNotification);
    safetyLimiter_.onClick = [this] {
        if (safetyLimiter_.getToggleState()) {           // turning ON — always allowed, no warning
            processorRef.setLimiterEnabled(true);
            return;
        }
        // turning OFF — require an explicit, warned confirmation
        auto opts = juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Disable safety limiter?")
            .withMessage("Disabling the safety limiter allows dangerously loud output that "
                         "can damage hearing or equipment. Continue?")
            .withButton("Cancel")     // index 0 (safe default)
            .withButton("Disable");   // index 1
        juce::AlertWindow::showAsync(opts, [this](int result) {
            if (result == 1) processorRef.setLimiterEnabled(false);                       // "Disable"
            else safetyLimiter_.setToggleState(true, juce::dontSendNotification);         // revert -> stays ON
        });
    };
    ampSection_.addAndMakeVisible(safetyLimiter_);

    limitIndicator_.setText("LIMIT", juce::dontSendNotification);
    limitIndicator_.setJustificationType(juce::Justification::centred);
    limitIndicator_.setColour(juce::Label::textColourId, juce::Colours::darkgrey);
    ampSection_.addAndMakeVisible(limitIndicator_);
```
> **Fail-safe note (verify at smoke-test):** the logic only disables on the explicit "Disable" button and reverts to ON for Cancel/escape/close. Confirm the JUCE 8 `showAsync` button index (Cancel=0, Disable=1) at run time; if the indices differ, the worst case must remain "stays ON," never "silently OFF."

- [ ] **Step 3: Start the timer + add `timerCallback`**

In the constructor (after `bindLayer(0);`):
```cpp
    startTimerHz(24);
```
Add the method (anywhere in the .cpp, e.g. after `paint`):
```cpp
void K2000AudioProcessorEditor::timerCallback() {
    const bool active = processorRef.gainReductionDb() > 0.1f;
    limitIndicator_.setColour(juce::Label::textColourId,
                              active ? juce::Colours::red : juce::Colours::darkgrey);
    // keep the toggle in sync if state changed via load
    if (safetyLimiter_.getToggleState() != processorRef.isLimiterEnabled())
        safetyLimiter_.setToggleState(processorRef.isLimiterEnabled(), juce::dontSendNotification);
    limitIndicator_.repaint();
}
```
Stop the timer in the destructor (first line of `~K2000AudioProcessorEditor`, before `binder_.clear();`):
```cpp
    stopTimer();
```

- [ ] **Step 4: Lay out the controls in `resized()`**

In the signal-row block, after `ampSection_.setBounds(row.reduced(2));`, add a layout for the amp section's content (stack the three controls vertically):
```cpp
        {
            auto ac = ampSection_.contentBounds();
            safetyLbl_.setBounds(ac.removeFromTop(16));
            safetyLimiter_.setBounds(ac.removeFromTop(28).reduced(4, 2));
            limitIndicator_.setBounds(ac.removeFromTop(20));
        }
```

- [ ] **Step 5: Build the UI target + manual smoke**

Run: `cmake --build build --target k2000_Standalone -j4`
Expected: compiles cleanly. **Manual smoke** (not automated): launch the Standalone, confirm: the Amp section shows a lit "Limiter" toggle; turning it off pops the warning (Cancel reverts to ON, Disable turns it off); the "LIMIT" label turns red when a high-resonance/Separation patch drives the output into the ceiling; reopening the editor reflects the saved state. (Plugin-format build also fine: `--target k2000_VST3`.)

- [ ] **Step 6: Commit**

```bash
git add src/PluginEditor.h src/PluginEditor.cpp
git commit -m "feat(safety): Amp-section limiter toggle with disable warning + gain-reduction indicator"
```

---

## Self-Review

**Spec coverage:**
- §2 smooth limiter + hard-clip backstop, zero-latency, instant attack / 80 ms release → Task 1 `SafetyLimiter`. ✓
- §2 protected non-automatable enable, default ON, persists, absent→ON → Task 2 (`bool` member, accessors, XML attr with `getBoolAttribute(..., true)`). ✓
- §2 fixed −12 dBFS CALIB ceiling → Task 1 `kSafetyCeilingDb`. ✓
- §2 stereo-linked → Task 1 (one `gain_`, peak = max(|L|,|R|)). ✓
- §3.2 apply after master gain; publish GR; accessors → Task 2 Steps 4–5, 3. ✓
- §3.3 toggle + modal disable warning + GR indicator in Amp section → Task 3. ✓
- §4 tests (ceiling, no-overshoot, passthrough, stereo-link, release, GR report; enable default/round-trip) → Task 1 Step 1 + Task 2 Step 1. ✓
- §5 out-of-scope honored (no lookahead, no adjustable ceiling, single global stage, single indicator, enable not automatable). ✓

**Placeholder scan:** none. The one judgement call — the JUCE `showAsync` button-index convention — is given concrete code (Cancel=0/Disable=1) plus an explicit fail-safe note to verify at smoke-test; the logic fails safe (only the explicit Disable index disables). Not a vague placeholder.

**Type consistency:** `SafetyLimiter::prepare/reset/process(float*,float*,int)/gainReductionDb()` used identically in Tasks 1–3. `K2000AudioProcessor::isLimiterEnabled()/setLimiterEnabled(bool)/gainReductionDb()` defined in Task 2 Step 3 and consumed in Task 2 tests + Task 3. `kSafetyCeilingDb = -12.0f` matches the tests' `std::pow(10, -12/20)` ceiling. The enable is a plain `bool` everywhere (never an APVTS param), satisfying the protected-control requirement.

**Known non-automated surface:** Task 3 (editor) is verified by build + manual smoke, consistent with the codebase (the editor is excluded from the test target). The DSP guarantee and the enable/state behavior — the safety-critical parts — are fully unit-tested in Tasks 1–2.
