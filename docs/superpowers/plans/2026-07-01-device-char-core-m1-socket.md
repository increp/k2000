# Device-Char Core — Milestone 1: The `DeviceUnderTest` Socket — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce a device-agnostic `DeviceUnderTest` contract (with a kind/excitation capability descriptor), make the existing filter socket implement it, and make the runner polymorphic over it — with **zero behavior change** (all existing tests stay green).

**Architecture:** Extract an abstract base `DeviceUnderTest` above today's concrete `FilterUnderTest`. Filters return `TransferFunction`/`InputSweep`; oscillators (SP-C) and hardware captures (SP-D) will implement the same base later. The L0 ruler already duck-types on a `reset()/process(buf,n)` adapter, so it measures any `DeviceUnderTest&` via virtual dispatch. `CharacterizationRunner::run` is retargeted from `FilterUnderTest&` to `DeviceUnderTest&`; derived→base reference binding keeps every existing call site compiling unchanged.

**Tech Stack:** C++17, JUCE 8 (`juce::UnitTest`), CMake. Namespace `chz`.

## Global Constraints

- **C++17**, JUCE 8, CMake. All new code in namespace `chz`.
- **Bounded build parallelism: `-j4`** — never bare `-j` (a JUCE compile OOMs → 0-byte object → confusing link failure).
- **Adapter contract** the L0 ruler needs: `void reset(); void process(float* mono, int n)`.
- **No DSP-voicing changes** — this milestone is test-harness plumbing only; shipping filter DSP is untouched (voicing is held for hardware).
- **Non-regression is the acceptance bar** — the full `k2000_tests` suite (and the opt-in `k2000_filter_characterization` target) must build and pass unchanged.
- Dev loop: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`.
- Commit messages end with: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

## Milestone sequence (context)

This is **Milestone 1 of SP-A** (spec: `docs/superpowers/specs/2026-07-01-device-characterization-core-design.md`). It is deliberately small and non-regressing — the foundation the rest ride on. Subsequent milestones get their own plans:
- **M2** — L0 absolute-level axis + synthetic known-answer references + their trust gates.
- **M3** — device-agnostic schema/summary + level-regression gates + rename the opt-in exe to `k2000_device_characterization`.
- **M4** — perceptual lenses (audibility-weighted aliasing, A-weighting) + physical-reference calibration capability.

Deferred out of M1 (intentionally): the exe rename (M3), any new metric (M2+), oscillator/hardware `DeviceUnderTest` implementations (SP-C/SP-D).

---

## File Structure

- **Create** `tests/characterization/DeviceUnderTest.h` — the `DeviceKind` + `Excitation` enums and the abstract `DeviceUnderTest` base. Header-only. One responsibility: the device-agnostic contract.
- **Create** `tests/DeviceUnderTestTests.cpp` — new contract tests (a passthrough test-double + the real factories' descriptors + polymorphic runner call).
- **Modify** `tests/characterization/FilterUnderTest.h` — derive from `DeviceUnderTest`; add `kind()`/`excitation()`; mark overrides.
- **Modify** `tests/characterization/CharacterizationRunner.h` and `.cpp` — `run()` and the private `runB{1,2,3}OnePoint` helpers take `DeviceUnderTest&` instead of `FilterUnderTest&`.
- **Modify** `tests/CMakeLists.txt` — add `DeviceUnderTestTests.cpp` to the `k2000_tests` target.

No `.cpp` is needed for `DeviceUnderTest` (pure interface). `FilterUnderTest.cpp` is unchanged (its method bodies already satisfy the base).

---

## Task 1: The `DeviceUnderTest` contract + a passthrough proof

**Files:**
- Create: `tests/characterization/DeviceUnderTest.h`
- Create: `tests/DeviceUnderTestTests.cpp`
- Modify: `tests/CMakeLists.txt:78-82` (add the test source)

**Interfaces:**
- Produces: `chz::DeviceKind { TransferFunction, Generator, Captured }`, `chz::Excitation { InputSweep, Trigger, MidiCapture }`, and the abstract `chz::DeviceUnderTest` with pure virtuals `reset()`, `process(float*,int)`, `name() const`, `kind() const`, `excitation() const`, `supports(Mode)`, `setOperatingPoint(const OperatingPoint&)`.

- [ ] **Step 1: Write the failing test**

Create `tests/DeviceUnderTestTests.cpp`:

```cpp
#include <juce_core/juce_core.h>
#include "characterization/DeviceUnderTest.h"
#include "testdsp/SteppedSine.h"
#include <cmath>
#include <vector>

using namespace chz;

// Minimal unity-gain passthrough device. Proves the DeviceUnderTest contract and
// that an L0 ruler engine can measure a device through the ABSTRACT BASE (virtual
// dispatch), independent of any real DSP.
struct PassthroughDevice : public DeviceUnderTest {
    void reset() override {}
    void process(float* /*mono*/, int /*n*/) override {}   // unity: leave buffer unchanged
    juce::String name() const override { return "passthrough"; }
    DeviceKind kind() const override { return DeviceKind::TransferFunction; }
    Excitation excitation() const override { return Excitation::InputSweep; }
    bool supports(Mode) override { return true; }
    void setOperatingPoint(const OperatingPoint&) override {}
};

struct DeviceUnderTestTests : public juce::UnitTest {
    DeviceUnderTestTests() : juce::UnitTest("DeviceUnderTest") {}

    void runTest() override {
        beginTest("descriptor reports kind / excitation / name");
        PassthroughDevice dev;
        DeviceUnderTest& dut = dev;                 // measured via the abstract base
        expect(dut.kind() == DeviceKind::TransferFunction);
        expect(dut.excitation() == Excitation::InputSweep);
        expectEquals(dut.name(), juce::String("passthrough"));

        beginTest("L0 ruler measures a device through the base (unity ~0 dB)");
        std::vector<double> freqs { 100.0, 1000.0, 10000.0 };
        auto r = testdsp::SteppedSine::transfer(dut, freqs, 48000.0, 0.25f);
        for (double m : r.magDb)
            expect(std::abs(m) < 0.01, "unity gain should read ~0 dB");
    }
};

static DeviceUnderTestTests deviceUnderTestTestsInstance;
```

Add the source to the `k2000_tests` target. In `tests/CMakeLists.txt`, change:

```cmake
    FilterUnderTestTests.cpp
    CharacterizationRunnerTests.cpp
    CharacterizationGateTests.cpp
    characterization/FilterUnderTest.cpp
    characterization/CharacterizationRunner.cpp)
```

to:

```cmake
    FilterUnderTestTests.cpp
    CharacterizationRunnerTests.cpp
    CharacterizationGateTests.cpp
    DeviceUnderTestTests.cpp
    characterization/FilterUnderTest.cpp
    characterization/CharacterizationRunner.cpp)
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: **build FAILS** — `fatal error: characterization/DeviceUnderTest.h: No such file or directory` (the type does not exist yet).

- [ ] **Step 3: Create the contract header**

Create `tests/characterization/DeviceUnderTest.h`:

```cpp
#pragma once
#include "OperatingPoint.h"
#include <juce_core/juce_core.h>

// chz -- the device-agnostic characterization socket. Any measurable device
// (filter, oscillator, or hardware capture) implements DeviceUnderTest. The L0
// ruler engines (SteppedSine / ESS) only need reset()+process(); the capability
// descriptor (kind / excitation / supports) tells the runner HOW to drive it.
namespace chz {

// What kind of device — determines how it is excited and what a battery measures.
enum class DeviceKind {
    TransferFunction,   // input -> output (filters): drive a signal through it
    Generator,          // produces output on its own (oscillators): trigger & record
    Captured            // pre-recorded / hardware capture (SP-D): replay captured audio
};

// How the runner excites a device. One driver per mode (only InputSweep is live in M1).
enum class Excitation {
    InputSweep,         // feed a probe / sweep through it (filters)
    Trigger,            // trigger a note / impulse and record the emission (oscillators)
    MidiCapture         // send MIDI to real hardware and capture (SP-D)
};

struct DeviceUnderTest {
    virtual ~DeviceUnderTest() = default;

    // L0 adapter contract -- any ruler engine can measure it.
    // For a Generator the input buffer is ignored and overwritten with output.
    virtual void reset()                     = 0;
    virtual void process(float* mono, int n) = 0;

    // Capability descriptor.
    virtual juce::String name()       const  = 0;
    virtual DeviceKind   kind()       const  = 0;
    virtual Excitation   excitation() const  = 0;
    virtual bool         supports(Mode m)    = 0;   // non-const: may set the model's mode
    virtual void         setOperatingPoint(const OperatingPoint& op) = 0;
};

} // namespace chz
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: builds; the run reports the `DeviceUnderTest` test group passing and **0 failures** overall (exit code 0).

- [ ] **Step 5: Commit**

```bash
git add tests/characterization/DeviceUnderTest.h tests/DeviceUnderTestTests.cpp tests/CMakeLists.txt
git commit -m "feat(chz): DeviceUnderTest contract (DeviceKind/Excitation) + passthrough proof" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: `FilterUnderTest` implements `DeviceUnderTest`

**Files:**
- Modify: `tests/characterization/FilterUnderTest.h:18-44`
- Modify: `tests/DeviceUnderTestTests.cpp` (extend with the factory-descriptor test)

**Interfaces:**
- Consumes: `chz::DeviceUnderTest`, `chz::DeviceKind`, `chz::Excitation` (Task 1); `chz::makeMoogFut()`, `chz::makeHuggettFut()` returning `std::unique_ptr<FilterUnderTest>` (existing).
- Produces: `FilterUnderTest : public DeviceUnderTest`, adding `kind() const -> TransferFunction` and `excitation() const -> InputSweep`.

- [ ] **Step 1: Write the failing test**

Extend `tests/DeviceUnderTestTests.cpp`: add the include near the top (after the existing includes)…

```cpp
#include "characterization/FilterUnderTest.h"
```

…and add these lines at the **end of** `runTest()` (after the existing `beginTest` blocks):

```cpp
        beginTest("real filter factories report TransferFunction / InputSweep");
        auto moog = chz::makeMoogFut();
        auto hug  = chz::makeHuggettFut();
        DeviceUnderTest& mdut = *moog;    // upcast proves FilterUnderTest IS-A DeviceUnderTest
        DeviceUnderTest& hdut = *hug;
        expect(mdut.kind() == DeviceKind::TransferFunction);
        expect(mdut.excitation() == Excitation::InputSweep);
        expect(hdut.kind() == DeviceKind::TransferFunction);
        expect(hdut.excitation() == Excitation::InputSweep);
        expectEquals(mdut.name(), juce::String("moog"));
        expectEquals(hdut.name(), juce::String("huggett"));
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: **build FAILS** — `FilterUnderTest` has no member `kind`, and it is not convertible to `DeviceUnderTest&` (it does not derive from it yet).

- [ ] **Step 3: Make `FilterUnderTest` derive from the base**

In `tests/characterization/FilterUnderTest.h`, add the include after line 2's includes:

```cpp
#include "DeviceUnderTest.h"
```

Change the class declaration line:

```cpp
class FilterUnderTest {
```

to:

```cpp
class FilterUnderTest : public DeviceUnderTest {
```

Then add `override` to the existing contract methods and add the two descriptor methods. Change this block:

```cpp
    juce::String name() const { return name_; }
    // NOT const: probing the configurator sets the model's mode/slope. setOperatingPoint()
    // always re-applies mode before measuring, so a prior supports() probe cannot leak into a
    // measurement — but never call supports() mid-sweep.
    bool supports(Mode m);

    void setOperatingPoint(const OperatingPoint& op);
    void reset();
    void process(float* mono, int n);   // base-rate in/out; OS applied internally
```

to:

```cpp
    juce::String name() const override { return name_; }
    DeviceKind   kind()       const override { return DeviceKind::TransferFunction; }
    Excitation   excitation() const override { return Excitation::InputSweep; }
    // NOT const: probing the configurator sets the model's mode/slope. setOperatingPoint()
    // always re-applies mode before measuring, so a prior supports() probe cannot leak into a
    // measurement — but never call supports() mid-sweep.
    bool supports(Mode m) override;

    void setOperatingPoint(const OperatingPoint& op) override;
    void reset() override;
    void process(float* mono, int n) override;   // base-rate in/out; OS applied internally
```

(`FilterUnderTest.cpp` needs no change — the method bodies already match these signatures.)

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: builds; `DeviceUnderTest` group passes (now including the factory descriptors); **0 failures** overall.

- [ ] **Step 5: Commit**

```bash
git add tests/characterization/FilterUnderTest.h tests/DeviceUnderTestTests.cpp
git commit -m "feat(chz): FilterUnderTest implements DeviceUnderTest (TransferFunction/InputSweep)" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Runner is polymorphic over `DeviceUnderTest`

**Files:**
- Modify: `tests/characterization/CharacterizationRunner.h:40,51-53,62-63,72-73`
- Modify: `tests/characterization/CharacterizationRunner.cpp` (parameter types on `run`, `runB1OnePoint`, `runB2OnePoint`, `runB3OnePoint`)
- Modify: `tests/DeviceUnderTestTests.cpp` (add the polymorphic-runner test)

**Interfaces:**
- Consumes: `chz::DeviceUnderTest&` (Task 1), `chz::makeMoogFut()`, `chz::Grid`, `chz::CharacterizationRunner::run`, `chz::CharacterizationRunner::logFreqs`.
- Produces: `CharacterizationRunner::run(DeviceUnderTest& dut, const Grid& g, const juce::File& outDir) -> Summary` (was `FilterUnderTest&`). Behavior identical; every existing `FilterUnderTest&` call site still binds via derived→base conversion.

- [ ] **Step 1: Write the failing test**

Extend `tests/DeviceUnderTestTests.cpp` — add the include near the top:

```cpp
#include "characterization/CharacterizationRunner.h"
```

…and add at the **end of** `runTest()`:

```cpp
        beginTest("runner accepts a DeviceUnderTest& (polymorphic)");
        auto moogForRun = chz::makeMoogFut();
        DeviceUnderTest& runDut = *moogForRun;          // pass as the abstract base
        chz::Grid g;
        g.modes       = { Mode::LP24 };
        g.cutoffs     = { 1000.0 };
        g.resonances  = { 0.0 };
        g.drives      = { 0.0 };
        g.osFactors   = { 1 };
        g.osModes     = { OsMode::Live };
        g.hostRates   = { 96000.0 };
        g.probeFreqs  = chz::CharacterizationRunner::logFreqs(50.0, 20000.0, 20);
        auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("chz_dut_runner_test");
        outDir.deleteRecursively();
        outDir.createDirectory();
        auto summary = chz::CharacterizationRunner::run(runDut, g, outDir);
        expect(summary.count("moog/LP24/fc1000/corner_hz") == 1,
               "runner should produce the corner_hz summary key via the base ref");
        outDir.deleteRecursively();
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --target k2000_tests -j4`
Expected: **build FAILS** — `run(runDut, …)` cannot bind a `DeviceUnderTest&` to the current `run(FilterUnderTest&)` parameter (no implicit base→derived conversion).

- [ ] **Step 3: Retarget the runner signatures**

In `tests/characterization/CharacterizationRunner.h`, add the include after line 2:

```cpp
#include "DeviceUnderTest.h"
```

Change the public `run` declaration:

```cpp
    static Summary run(FilterUnderTest& fut, const Grid& g, const juce::File& outDir);
```

to:

```cpp
    static Summary run(DeviceUnderTest& dut, const Grid& g, const juce::File& outDir);
```

Change the three private helper declarations' first parameter from `FilterUnderTest& fut` to `DeviceUnderTest& dut`:

```cpp
    static B1Result runB1OnePoint(DeviceUnderTest& dut, const OperatingPoint& op,
                                   const std::vector<double>& probeFreqs,
                                   juce::String& csvRows);

    static B2Result runB2OnePoint(DeviceUnderTest& dut, const OperatingPoint& op,
                                   juce::String& csvRows);

    static B3Result runB3OnePoint(DeviceUnderTest& dut, const OperatingPoint& op,
                                   double probeHz, juce::String& csvRows);
```

In `tests/characterization/CharacterizationRunner.cpp`, change the corresponding definitions' first parameter from `FilterUnderTest& fut` to `DeviceUnderTest& fut` in all four functions (`run`, `runB1OnePoint`, `runB2OnePoint`, `runB3OnePoint`). **Keep the parameter name `fut`** so no method bodies change — only the type on the signature line changes. For example, `run`'s definition line:

```cpp
Summary CharacterizationRunner::run(FilterUnderTest& fut, const Grid& g,
                                     const juce::File& outDir) {
```

becomes:

```cpp
Summary CharacterizationRunner::run(DeviceUnderTest& fut, const Grid& g,
                                     const juce::File& outDir) {
```

(Do the same one-word type change on the `runB1OnePoint`, `runB2OnePoint`, and `runB3OnePoint` definition lines. The bodies call `fut.name()/supports()/setOperatingPoint()/reset()/process()` and pass `fut` to the templated `SteppedSine::transfer` / `EssResponse::measure` / `Harmonics::thdDb`, all of which resolve via virtual dispatch — no body edits.)

- [ ] **Step 4: Run to verify it passes (full suite — the non-regression gate)**

Run: `cmake --build build --target k2000_tests -j4 && ./build/tests/k2000_tests`
Expected: builds; the new polymorphic-runner test passes; and every pre-existing test group (`FilterUnderTest`, `CharacterizationRunner`, `CharacterizationGate`, all others) still passes — **0 failures** overall. This green run is the non-regression proof: existing `run(*fut, …)` call sites in `CharacterizationGateTests.cpp`, `CharacterizationRunnerTests.cpp`, and `characterize_main.cpp` compile unchanged because `FilterUnderTest&` converts to `DeviceUnderTest&`.

- [ ] **Step 5: Verify the opt-in heavy target also still builds**

Run: `cmake --build build --target k2000_filter_characterization -j4`
Expected: builds cleanly (its `characterize_main.cpp` calls `run(*fut, …)`, still valid via the base conversion).

- [ ] **Step 6: Commit**

```bash
git add tests/characterization/CharacterizationRunner.h tests/characterization/CharacterizationRunner.cpp tests/DeviceUnderTestTests.cpp
git commit -m "refactor(chz): CharacterizationRunner is polymorphic over DeviceUnderTest" -m "Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**1. Spec coverage (M1 scope only).** SP-A §3 "L1 device socket (`DeviceUnderTest`) generalizes `FilterUnderTest`… declares kind + excitation… runner is device-agnostic" → Tasks 1–3. SP-A §8 "the existing Huggett/Moog filter batteries ported onto the Core — same numbers, nothing regresses" → Task 3 Steps 4–5 (full suite green + opt-in target builds). The broader SP-A items (level axis, trust references, schema, perceptual, physical-reference) are explicitly assigned to M2–M4 in the Milestone Sequence section — not gaps.

**2. Placeholder scan.** No TBD/TODO. Every code step shows complete content; every run step shows an exact command + expected result. The only "stub" is conceptual (`Trigger`/`MidiCapture` enum values are defined but have no driver yet) — that is intended scope, documented in the header comment, not a plan placeholder.

**3. Type consistency.** `DeviceUnderTest`, `DeviceKind::{TransferFunction,Generator,Captured}`, `Excitation::{InputSweep,Trigger,MidiCapture}` are defined in Task 1 and used identically in Tasks 2–3. `FilterUnderTest`'s overridden signatures (`name() const`, `kind() const`, `excitation() const`, `supports(Mode)`, `setOperatingPoint(const OperatingPoint&)`, `reset()`, `process(float*,int)`) match the base's pure virtuals exactly. `run(DeviceUnderTest&, const Grid&, const juce::File&) -> Summary` is consistent across the header, the `.cpp` definition, and the Task 3 test call.

---

## Execution Handoff

Two execution options:
1. **Subagent-Driven (recommended)** — a fresh subagent per task, two-stage review between tasks.
2. **Inline Execution** — execute the tasks in this session with checkpoints.
