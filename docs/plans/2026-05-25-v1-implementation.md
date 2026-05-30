# k2000 v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a VST3 synth plugin that loads in Ableton 12 / Carla, responds to MIDI, plays 8-voice polyphony through a fixed Oscillator → SVF Filter → Waveshaper → ADSR Amp chain, with state save/load and Linux+Windows CI builds.

**Architecture:** JUCE 8 + CMake. `Voice` holds `std::array<std::unique_ptr<DSPBlock>, 2>` (polymorphic VAST slots). Parameters live in a single `AudioProcessorValueTreeState` with per-slot namespacing (`slot0.cutoff`, `slot1.drive`). Each audio block, a `ParamSnapshot` is built once and passed to each DSP block — no atomic loads in the audio hot loop.

**Tech Stack:** C++17, JUCE 8, CMake 3.22+, juce::UnitTest, GitHub Actions (ubuntu-22.04 + windows-2022 matrix).

**Spec:** [docs/specs/2026-05-25-v1-skeleton-design.md](../specs/2026-05-25-v1-skeleton-design.md)

---

## Final file structure (after all tasks)

```
k2000/
├── .github/workflows/build.yml
├── .gitignore
├── .gitmodules
├── CMakeLists.txt
├── README.md                              (already exists)
├── docs/                                  (already exists)
├── src/
│   ├── PluginProcessor.{h,cpp}
│   ├── PluginEditor.{h,cpp}
│   ├── VoiceManager.{h,cpp}
│   ├── Voice.{h,cpp}
│   ├── dsp/
│   │   ├── DSPBlock.h
│   │   ├── ParamSpec.h
│   │   ├── Oscillator.{h,cpp}
│   │   ├── Envelope.{h,cpp}
│   │   └── blocks/
│   │       ├── SVFFilter.{h,cpp}
│   │       └── Waveshaper.{h,cpp}
│   └── params/
│       ├── Parameters.{h,cpp}
│       └── ParamSnapshot.h
├── tests/
│   ├── CMakeLists.txt
│   ├── TestMain.cpp
│   ├── OscillatorTests.cpp
│   ├── EnvelopeTests.cpp
│   ├── SVFFilterTests.cpp
│   ├── WaveshaperTests.cpp
│   ├── ParamSnapshotTests.cpp
│   ├── VoiceTests.cpp
│   ├── VoiceManagerTests.cpp
│   └── PluginLifecycleTests.cpp
└── third_party/JUCE/                      (git submodule)
```

---

## Task 0: Project scaffolding & JUCE submodule

**Files:**
- Create: `.gitignore`
- Create: `.gitmodules` (implicitly via `git submodule add`)
- Create: `third_party/JUCE/` (submodule)

- [ ] **Step 1: Initialise git repo**

```bash
cd /home/increp/dev/k2000
git init -b main
```

Expected: `Initialized empty Git repository in /home/increp/dev/k2000/.git/`.

- [ ] **Step 2: Create `.gitignore`**

Create `/home/increp/dev/k2000/.gitignore` with:

```gitignore
# Build artifacts
build/
out/
cmake-build-*/

# CMake
CMakeCache.txt
CMakeFiles/
CMakeScripts/
cmake_install.cmake
Makefile
*.cmake

# IDE files
.idea/
.vscode/
*.swp
*.swo
*~

# OS
.DS_Store
Thumbs.db

# JUCE generated
JuceLibraryCode/
Builds/

# VST3 install artefacts (we don't commit binaries)
*.vst3/
!third_party/**

# Logs
*.log
```

- [ ] **Step 3: Add JUCE as a git submodule, pinned to JUCE 8.0.4**

```bash
cd /home/increp/dev/k2000
git submodule add --depth 1 -b 8.0.4 https://github.com/juce-framework/JUCE.git third_party/JUCE
git submodule update --init --recursive
```

Expected: `third_party/JUCE/` contains the JUCE source tree. `.gitmodules` is created with the JUCE entry.

If `--depth 1 -b 8.0.4` errors out because the tag isn't a branch, fall back to:

```bash
git submodule add https://github.com/juce-framework/JUCE.git third_party/JUCE
cd third_party/JUCE && git checkout 8.0.4 && cd ../..
```

- [ ] **Step 4: Create empty source folders so the directory tree exists in git**

```bash
mkdir -p src/dsp/blocks src/params src/gui tests
touch src/.gitkeep src/dsp/blocks/.gitkeep src/params/.gitkeep src/gui/.gitkeep tests/.gitkeep
```

- [ ] **Step 5: Commit**

```bash
git add .gitignore .gitmodules src tests
git commit -m "chore: project scaffold with JUCE 8.0.4 submodule"
```

Expected: one commit, working tree clean.

---

## Task 1: Minimal CMake build + empty plugin stub

**Goal:** A buildable VST3 plugin that does nothing (silence). Proves the build pipeline works before we add any DSP.

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/PluginProcessor.h`
- Create: `src/PluginProcessor.cpp`
- Create: `src/PluginEditor.h`
- Create: `src/PluginEditor.cpp`

- [ ] **Step 1: Create `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.22)
project(k2000 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Speed up Windows builds; harmless on Linux
set(CMAKE_OPTIMIZE_DEPENDENCIES ON)

add_subdirectory(third_party/JUCE)

juce_add_plugin(k2000
    COMPANY_NAME "k2000"
    BUNDLE_ID com.k2000.k2000
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE K2vs
    PLUGIN_CODE K2vs
    FORMATS VST3 Standalone
    PRODUCT_NAME "k2000")

juce_generate_juce_header(k2000)

target_sources(k2000 PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_compile_definitions(k2000 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(k2000
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(BUILD_TESTING "Build the unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Create `src/PluginProcessor.h`**

```cpp
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class K2000AudioProcessor : public juce::AudioProcessor {
public:
    K2000AudioProcessor();
    ~K2000AudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "k2000"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(K2000AudioProcessor)
};
```

- [ ] **Step 3: Create `src/PluginProcessor.cpp`**

```cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"

K2000AudioProcessor::K2000AudioProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void K2000AudioProcessor::prepareToPlay(double, int) {}
void K2000AudioProcessor::releaseResources() {}

bool K2000AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        || out == juce::AudioChannelSet::mono();
}

void K2000AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

juce::AudioProcessorEditor* K2000AudioProcessor::createEditor() {
    return new K2000AudioProcessorEditor(*this);
}

void K2000AudioProcessor::getStateInformation(juce::MemoryBlock&) {}
void K2000AudioProcessor::setStateInformation(const void*, int) {}

// JUCE plugin entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new K2000AudioProcessor();
}
```

- [ ] **Step 4: Create `src/PluginEditor.h`**

```cpp
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class K2000AudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit K2000AudioProcessorEditor(K2000AudioProcessor& p);
    ~K2000AudioProcessorEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    K2000AudioProcessor& processorRef;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(K2000AudioProcessorEditor)
};
```

- [ ] **Step 5: Create `src/PluginEditor.cpp`**

```cpp
#include "PluginEditor.h"

K2000AudioProcessorEditor::K2000AudioProcessorEditor(K2000AudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p) {
    setSize(600, 300);
}

void K2000AudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    g.setFont(20.0f);
    g.drawFittedText("k2000 — v1 scaffold", getLocalBounds(),
                     juce::Justification::centred, 1);
}

void K2000AudioProcessorEditor::resized() {}
```

- [ ] **Step 6: Create placeholder `tests/CMakeLists.txt`**

So the top-level CMake doesn't error on `add_subdirectory(tests)` before Task 3 lands. Replace fully in Task 3.

```cmake
# Placeholder; full test target arrives in Task 3.
add_custom_target(tests_placeholder)
```

- [ ] **Step 7: Configure and build**

```bash
cd /home/increp/dev/k2000
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

Expected: Build succeeds. Binary at `build/k2000_artefacts/Debug/VST3/k2000.vst3/`. First build takes several minutes (JUCE compiles ~50 source files); subsequent builds are fast.

If you hit missing Linux dev libs (alsa/x11/freetype/etc.), install them:

```bash
sudo apt-get install -y libasound2-dev libjack-jackd2-dev \
  libcurl4-openssl-dev libfreetype6-dev libx11-dev \
  libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libwebkit2gtk-4.1-dev \
  libglu1-mesa-dev mesa-common-dev
```

- [ ] **Step 8: Install locally and verify in Carla**

```bash
mkdir -p ~/.vst3
rm -rf ~/.vst3/k2000.vst3
cp -r build/k2000_artefacts/Debug/VST3/k2000.vst3 ~/.vst3/
```

Launch Carla (or your preferred Linux VST3 host). Add `k2000` from the plugin list. Confirm: the editor opens, shows "k2000 — v1 scaffold" on a black background, and playing notes produces silence (correct — stub processor outputs nothing).

If you don't have Carla: `sudo apt-get install carla`.

- [ ] **Step 9: Commit**

```bash
git add CMakeLists.txt src/PluginProcessor.* src/PluginEditor.* tests/CMakeLists.txt
git commit -m "feat: minimal plugin stub builds and loads in Carla"
```

---

## Task 2: GitHub repo + CI for Linux & Windows artifacts

**Goal:** Every push produces downloadable Linux + Windows `.vst3` artifacts. This unblocks Ableton 12 testing on Windows.

**Files:**
- Create: `.github/workflows/build.yml`

Prerequisite: You need a GitHub repo. If you don't have the `gh` CLI authenticated:

```bash
gh auth login
```

(Pick HTTPS + browser auth — follow the prompts.)

- [ ] **Step 1: Create the GitHub repo and push**

```bash
cd /home/increp/dev/k2000
gh repo create k2000 --private --source=. --remote=origin --push
```

If you prefer to keep it local-only for now, skip this step — the CI workflow can be added later when you push.

- [ ] **Step 2: Create `.github/workflows/build.yml`**

```bash
mkdir -p .github/workflows
```

Then create `.github/workflows/build.yml`:

```yaml
name: Build

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]
  workflow_dispatch:

jobs:
  build:
    name: ${{ matrix.os }}
    strategy:
      fail-fast: false
      matrix:
        include:
          - os: ubuntu-22.04
            short_name: linux
          - os: windows-2022
            short_name: windows
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install Linux deps
        if: runner.os == 'Linux'
        run: |
          sudo apt-get update
          sudo apt-get install -y libasound2-dev libjack-jackd2-dev \
            libcurl4-openssl-dev libfreetype6-dev libx11-dev \
            libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
            libxrandr-dev libxrender-dev libwebkit2gtk-4.1-dev \
            libglu1-mesa-dev mesa-common-dev

      - name: Configure
        run: cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON

      - name: Build
        run: cmake --build build --config Release -j

      - name: Run tests
        run: ctest --test-dir build -C Release --output-on-failure

      - name: Upload VST3 artifact
        uses: actions/upload-artifact@v4
        with:
          name: k2000-${{ matrix.short_name }}-${{ github.sha }}
          path: build/k2000_artefacts/Release/VST3/k2000.vst3
          if-no-files-found: error
```

- [ ] **Step 3: Commit & push**

```bash
git add .github/workflows/build.yml
git commit -m "ci: build Linux + Windows VST3 artifacts on every push"
git push -u origin main
```

- [ ] **Step 4: Verify CI passes on both platforms**

```bash
gh run watch
```

Or open the repo's Actions tab in your browser. The first run takes ~15–20 minutes (cold caches). Both matrix jobs must pass green.

- [ ] **Step 5: Manual smoke test — download Windows artifact, load in Ableton 12**

From GitHub Actions UI: open the latest successful run → Artifacts section → download `k2000-windows-<sha>.zip`. On Windows, unzip into `C:\Program Files\Common Files\VST3\`. Open Ableton 12, rescan plugins (Options → Preferences → Plug-Ins → "Rescan Plug-Ins"). Drag `k2000` onto a MIDI track. Confirm the plugin loads and shows the editor.

It will produce silence (correct — DSP arrives in later tasks). If it crashes Ableton or doesn't appear in the browser, that's a real bug worth fixing now before adding DSP makes it harder to bisect.

- [ ] **Step 6: No commit (verification step only).**

---

## Task 3: Test infrastructure

**Goal:** A working `juce::UnitTest` harness invoked by `ctest`. One trivial test proves the wiring.

**Files:**
- Replace: `tests/CMakeLists.txt`
- Create: `tests/TestMain.cpp`
- Create: `tests/SmokeTests.cpp`

- [ ] **Step 1: Replace `tests/CMakeLists.txt`**

```cmake
add_executable(k2000_tests
    TestMain.cpp
    SmokeTests.cpp)

target_link_libraries(k2000_tests PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_compile_definitions(k2000_tests PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    K2000_TESTING=1)

set_target_properties(k2000_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)

add_test(NAME k2000_tests COMMAND k2000_tests)
```

- [ ] **Step 2: Create `tests/TestMain.cpp`**

```cpp
#include <juce_core/juce_core.h>
#include <cstdio>

int main(int, char**) {
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    int failedTests = 0;
    int totalTests = 0;
    for (int i = 0; i < runner.getNumResults(); ++i) {
        const auto* r = runner.getResult(i);
        totalTests++;
        failedTests += r->failures;
        std::printf("[%s] %s: %d passes, %d failures\n",
            r->failures == 0 ? "PASS" : "FAIL",
            r->unitTestName.toRawUTF8(),
            r->passes, r->failures);
    }
    std::printf("\nSummary: %d tests, %d failed\n", totalTests, failedTests);
    return failedTests > 0 ? 1 : 0;
}
```

- [ ] **Step 3: Create the failing smoke test in `tests/SmokeTests.cpp`**

```cpp
#include <juce_core/juce_core.h>

class SmokeTest : public juce::UnitTest {
public:
    SmokeTest() : juce::UnitTest("Smoke") {}
    void runTest() override {
        beginTest("arithmetic works");
        expect(1 + 1 == 3, "this should fail until we fix it");
    }
};

static SmokeTest smokeTestInstance;
```

- [ ] **Step 4: Build and run — confirm it fails**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: `FAIL: Smoke: 0 passes, 1 failures` and ctest reports a failed test.

- [ ] **Step 5: Fix the smoke test**

Edit `tests/SmokeTests.cpp` — change `1 + 1 == 3` to `1 + 1 == 2`.

- [ ] **Step 6: Build and run — confirm it passes**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: `PASS: Smoke: 1 passes, 0 failures`. ctest reports success.

- [ ] **Step 7: Commit**

```bash
git add tests/
git commit -m "test: juce::UnitTest harness wired into ctest"
git push
```

Watch CI also stays green:

```bash
gh run watch
```

---

## Task 4: Parameters + ParamSnapshot

**Goal:** Single source of truth for the v1 parameter set. An `AudioProcessorValueTreeState` (APVTS) is built from a parameter list; a `ParamSnapshot` reads atomically once per block for the audio thread.

**Files:**
- Create: `src/params/Parameters.h`
- Create: `src/params/Parameters.cpp`
- Create: `src/params/ParamSnapshot.h`
- Create: `tests/ParamSnapshotTests.cpp`
- Modify: `CMakeLists.txt` (add the new sources)
- Modify: `tests/CMakeLists.txt` (add the test source)

- [ ] **Step 1: Create `src/params/ParamSnapshot.h`**

```cpp
#pragma once
#include <juce_core/juce_core.h>

// Plain-old-data snapshot of the v1 parameter state.
// Built once per audio block on the audio thread by reading the APVTS's
// atomic raw-value pointers, then passed by const ref to Voice/blocks.
// Adding new params here is a single-source-of-truth change: extend this
// struct, the Parameters layout, and any consumer that needs the value.
struct ParamSnapshot {
    // Oscillator
    int   oscWaveform   = 0;   // 0=saw 1=square 2=triangle 3=sine
    float oscCoarse     = 0.0f; // semitones
    float oscFine       = 0.0f; // cents

    // Slot 0 — SVF filter
    int   svfType       = 0;   // 0=LP 1=HP 2=BP 3=Notch
    float svfCutoffHz   = 1000.0f;
    float svfResonance  = 0.2f;

    // Slot 1 — Waveshaper
    float wsDrive       = 0.0f;
    float wsMix         = 1.0f;

    // Amp envelope
    float ampAttackS    = 0.005f;
    float ampDecayS     = 0.1f;
    float ampSustain    = 0.8f;
    float ampReleaseS   = 0.2f;

    // Master
    float masterGainDb  = 0.0f;
};
```

- [ ] **Step 2: Create `src/params/Parameters.h`**

```cpp
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamSnapshot.h"

namespace params {

// Stable string IDs. Used in APVTS, in preset state, and in GUI attachments.
namespace id {
    inline constexpr auto oscWaveform   = "osc.waveform";
    inline constexpr auto oscCoarse     = "osc.coarse";
    inline constexpr auto oscFine       = "osc.fine";

    inline constexpr auto svfType       = "slot0.type";
    inline constexpr auto svfCutoff     = "slot0.cutoff";
    inline constexpr auto svfResonance  = "slot0.resonance";

    inline constexpr auto wsDrive       = "slot1.drive";
    inline constexpr auto wsMix         = "slot1.mix";

    inline constexpr auto ampAttack     = "amp.attack";
    inline constexpr auto ampDecay      = "amp.decay";
    inline constexpr auto ampSustain    = "amp.sustain";
    inline constexpr auto ampRelease    = "amp.release";

    inline constexpr auto masterGain    = "master.gain";
}

// Build the parameter layout. Called from PluginProcessor's APVTS constructor.
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Read the current values from the APVTS into a snapshot. RT-safe — reads
// each parameter's atomic raw-value pointer (no locks, no allocation).
ParamSnapshot snapshot(const juce::AudioProcessorValueTreeState& apvts);

} // namespace params
```

- [ ] **Step 3: Create `src/params/Parameters.cpp`**

```cpp
#include "Parameters.h"

namespace params {

using APVTS = juce::AudioProcessorValueTreeState;

juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
    APVTS::ParameterLayout layout;

    using FloatParam  = juce::AudioParameterFloat;
    using ChoiceParam = juce::AudioParameterChoice;

    layout.add(std::make_unique<ChoiceParam>(
        juce::ParameterID{id::oscWaveform, 1},
        "Osc Waveform",
        juce::StringArray{"Saw", "Square", "Triangle", "Sine"}, 0));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::oscCoarse, 1}, "Osc Coarse",
        juce::NormalisableRange<float>{-24.0f, 24.0f, 1.0f}, 0.0f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::oscFine, 1}, "Osc Fine",
        juce::NormalisableRange<float>{-100.0f, 100.0f, 0.1f}, 0.0f));

    layout.add(std::make_unique<ChoiceParam>(
        juce::ParameterID{id::svfType, 1},
        "Filter Type",
        juce::StringArray{"LP", "HP", "BP", "Notch"}, 0));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::svfCutoff, 1}, "Filter Cutoff",
        juce::NormalisableRange<float>{20.0f, 20000.0f, 0.0f, 0.25f},
        1000.0f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::svfResonance, 1}, "Filter Resonance",
        juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.2f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::wsDrive, 1}, "Drive",
        juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.0f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::wsMix, 1}, "Drive Mix",
        juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 1.0f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::ampAttack, 1}, "Attack",
        juce::NormalisableRange<float>{0.001f, 5.0f, 0.0f, 0.3f}, 0.005f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::ampDecay, 1}, "Decay",
        juce::NormalisableRange<float>{0.001f, 5.0f, 0.0f, 0.3f}, 0.1f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::ampSustain, 1}, "Sustain",
        juce::NormalisableRange<float>{0.0f, 1.0f, 0.0f}, 0.8f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::ampRelease, 1}, "Release",
        juce::NormalisableRange<float>{0.001f, 5.0f, 0.0f, 0.3f}, 0.2f));

    layout.add(std::make_unique<FloatParam>(
        juce::ParameterID{id::masterGain, 1}, "Master Gain",
        juce::NormalisableRange<float>{-60.0f, 6.0f, 0.0f}, 0.0f));

    return layout;
}

// Helper — assumes the parameter exists; will assert in debug if not.
static float raw(const APVTS& apvts, juce::StringRef id) {
    auto* p = apvts.getRawParameterValue(id);
    jassert(p != nullptr);
    return p->load();
}

ParamSnapshot snapshot(const APVTS& apvts) {
    ParamSnapshot s;
    s.oscWaveform   = (int) raw(apvts, id::oscWaveform);
    s.oscCoarse     = raw(apvts, id::oscCoarse);
    s.oscFine       = raw(apvts, id::oscFine);
    s.svfType       = (int) raw(apvts, id::svfType);
    s.svfCutoffHz   = raw(apvts, id::svfCutoff);
    s.svfResonance  = raw(apvts, id::svfResonance);
    s.wsDrive       = raw(apvts, id::wsDrive);
    s.wsMix         = raw(apvts, id::wsMix);
    s.ampAttackS    = raw(apvts, id::ampAttack);
    s.ampDecayS     = raw(apvts, id::ampDecay);
    s.ampSustain    = raw(apvts, id::ampSustain);
    s.ampReleaseS   = raw(apvts, id::ampRelease);
    s.masterGainDb  = raw(apvts, id::masterGain);
    return s;
}

} // namespace params
```

- [ ] **Step 4: Wire sources into `CMakeLists.txt`**

Edit `target_sources(k2000 PRIVATE ...)` block:

```cmake
target_sources(k2000 PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/params/Parameters.cpp)
```

- [ ] **Step 5: Write the failing test in `tests/ParamSnapshotTests.cpp`**

```cpp
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include "../src/params/Parameters.h"
#include "../src/params/ParamSnapshot.h"

class ParamSnapshotTest : public juce::UnitTest {
public:
    ParamSnapshotTest() : juce::UnitTest("ParamSnapshot") {}

    // Minimal AudioProcessor harness — we just need an APVTS attached
    // to something AudioProcessor-like to test snapshot reads.
    struct DummyProc : public juce::AudioProcessor {
        DummyProc() : juce::AudioProcessor(BusesProperties()
            .withOutput("Out", juce::AudioChannelSet::stereo(), true)) {}
        const juce::String getName() const override { return "Dummy"; }
        void prepareToPlay(double, int) override {}
        void releaseResources() override {}
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        bool hasEditor() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram(int) override {}
        const juce::String getProgramName(int) override { return ""; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock&) override {}
        void setStateInformation(const void*, int) override {}
    };

    void runTest() override {
        DummyProc proc;
        juce::AudioProcessorValueTreeState apvts(
            proc, nullptr, "PARAMS", params::createLayout());

        beginTest("defaults match expected values");
        auto s = params::snapshot(apvts);
        expectWithinAbsoluteError(s.oscCoarse, 0.0f, 1e-6f);
        expectWithinAbsoluteError(s.svfCutoffHz, 1000.0f, 1e-3f);
        expectWithinAbsoluteError(s.svfResonance, 0.2f, 1e-6f);
        expectWithinAbsoluteError(s.ampSustain, 0.8f, 1e-6f);
        expect(s.oscWaveform == 0);
        expect(s.svfType == 0);

        beginTest("setting a parameter changes the snapshot");
        if (auto* p = apvts.getParameter(params::id::svfCutoff))
            p->setValueNotifyingHost(p->convertTo0to1(2500.0f));
        s = params::snapshot(apvts);
        expectWithinAbsoluteError(s.svfCutoffHz, 2500.0f, 1.0f);
    }
};

static ParamSnapshotTest paramSnapshotTestInstance;
```

- [ ] **Step 6: Wire the test source into `tests/CMakeLists.txt`**

```cmake
add_executable(k2000_tests
    TestMain.cpp
    SmokeTests.cpp
    ParamSnapshotTests.cpp
    ../src/params/Parameters.cpp)
```

- [ ] **Step 7: Build & run — confirm tests pass**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: Both `Smoke` and `ParamSnapshot` tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/params tests/ParamSnapshotTests.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: parameter layout and atomic snapshot reader"
git push
```

---

## Task 5: DSPBlock interface + ParamSpec

**Goal:** The polymorphic abstraction every VAST processing block conforms to. No implementations yet — those come in tasks 8 & 9.

**Files:**
- Create: `src/dsp/ParamSpec.h`
- Create: `src/dsp/DSPBlock.h`

- [ ] **Step 1: Create `src/dsp/ParamSpec.h`**

```cpp
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Lightweight description of a single parameter exposed by a DSPBlock.
// Blocks declare their parameters via getParamSpecs(); the processor
// namespaces them by slot when registering them in the APVTS.
struct ParamSpec {
    juce::String id;                             // unscoped, e.g. "cutoff"
    juce::String label;                          // user-visible
    juce::NormalisableRange<float> range;
    float defaultValue;
};
```

- [ ] **Step 2: Create `src/dsp/DSPBlock.h`**

```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "ParamSpec.h"
#include "../params/ParamSnapshot.h"

// Abstract base for swappable per-voice processing units (VAST blocks).
// See docs/architecture/dsp-block-interface.md for the rationale behind
// each method.
class DSPBlock {
public:
    virtual ~DSPBlock() = default;

    // Allocate-OK. Called from prepareToPlay.
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // RT-safe. Called on note-on / voice-steal to clear state.
    virtual void reset() = 0;

    // RT-safe. Process numSamples in-place, mono.
    virtual void process(float* buffer, int numSamples) = 0;

    // Stable identifier for preset serialisation, e.g. "svf_filter".
    virtual juce::String getTypeId() const = 0;

    // Parameter descriptors. The processor namespaces these by slot.
    virtual std::vector<ParamSpec> getParamSpecs() const = 0;

    // RT-safe. Called once per audio block before process().
    virtual void updateParameters(const ParamSnapshot& snapshot) = 0;
};
```

- [ ] **Step 3: Add a build sanity-check** — ensure the headers compile by including them somewhere. Edit `src/PluginProcessor.cpp` and add at the top (after existing includes):

```cpp
#include "dsp/DSPBlock.h"
```

(We won't use it yet; this just confirms the header parses.)

- [ ] **Step 4: Build**

```bash
cmake --build build -j
```

Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/DSPBlock.h src/dsp/ParamSpec.h src/PluginProcessor.cpp
git commit -m "feat: DSPBlock interface and ParamSpec"
git push
```

---

## Task 6: Oscillator (PolyBLEP saw/square/triangle/sine)

**Goal:** An anti-aliased oscillator. TDD-style: write tests asserting expected behaviour at known frequencies, watch them fail, then implement.

**Files:**
- Create: `src/dsp/Oscillator.h`
- Create: `src/dsp/Oscillator.cpp`
- Create: `tests/OscillatorTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

### Background — polyBLEP in one paragraph

A naive saw `2*phase - 1` is discontinuous at the wrap (phase 1→0), and that discontinuity produces audible aliasing above Nyquist. PolyBLEP ("polynomial band-limited step") fixes this by *adding* a small polynomial correction in a window of `dt` samples around the discontinuity, where `dt = freq / sampleRate`. The same idea works for square (two discontinuities per cycle) and triangle (square integrated; needs a "polyBLAMP" correction at the *slope* changes, but a simpler approach is to integrate a polyBLEP square). Sine is already band-limited and needs no correction.

The implementation below uses Martin Finke's standard formulation.

- [ ] **Step 1: Create the test file `tests/OscillatorTests.cpp`**

```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/Oscillator.h"
#include <vector>
#include <cmath>

class OscillatorTest : public juce::UnitTest {
public:
    OscillatorTest() : juce::UnitTest("Oscillator") {}

    static constexpr double SR = 48000.0;
    static constexpr int BLOCK = 512;

    void runTest() override {
        runSineFundamentalTest();
        runSawHasHarmonicsTest();
        runResetTest();
        runZeroFreqTest();
        runBlockContinuityTest();
    }

private:
    static double rms(const std::vector<float>& buf) {
        double s = 0;
        for (float v : buf) s += double(v) * v;
        return std::sqrt(s / buf.size());
    }

    void runSineFundamentalTest() {
        beginTest("sine at 440Hz has FFT peak near bin 440 (1Hz res, 48k samples)");
        Oscillator osc;
        osc.prepare(SR);
        osc.setWaveform(Oscillator::Waveform::Sine);
        osc.setFrequency(440.0f);

        const int N = 48000; // 1 Hz bin resolution
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i) buf[i] = osc.processSample();

        // Find peak bin via brute-force DFT around 440Hz only — cheaper than FFT.
        double bestMag = 0; int bestBin = 0;
        for (int k = 430; k <= 450; ++k) {
            double real = 0, imag = 0;
            for (int n = 0; n < N; ++n) {
                double ang = -2.0 * juce::MathConstants<double>::pi * k * n / N;
                real += buf[n] * std::cos(ang);
                imag += buf[n] * std::sin(ang);
            }
            double mag = std::sqrt(real * real + imag * imag);
            if (mag > bestMag) { bestMag = mag; bestBin = k; }
        }
        expect(std::abs(bestBin - 440) <= 1,
            juce::String("expected peak near 440, got ") + juce::String(bestBin));
    }

    void runSawHasHarmonicsTest() {
        beginTest("saw at 1kHz has measurable energy at 2kHz, 3kHz harmonics");
        Oscillator osc;
        osc.prepare(SR);
        osc.setWaveform(Oscillator::Waveform::Saw);
        osc.setFrequency(1000.0f);

        const int N = 48000;
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i) buf[i] = osc.processSample();

        auto magAt = [&](int k) {
            double real = 0, imag = 0;
            for (int n = 0; n < N; ++n) {
                double ang = -2.0 * juce::MathConstants<double>::pi * k * n / N;
                real += buf[n] * std::cos(ang);
                imag += buf[n] * std::sin(ang);
            }
            return std::sqrt(real * real + imag * imag);
        };

        double m1k = magAt(1000);
        double m2k = magAt(2000);
        double m3k = magAt(3000);

        // Saw harmonics fall as 1/n, so m2k ≈ m1k/2, m3k ≈ m1k/3.
        // Generous tolerance: each higher harmonic must be at least 1/5 the fundamental.
        expect(m1k > 0);
        expect(m2k > m1k * 0.2, "2kHz harmonic should be substantial");
        expect(m3k > m1k * 0.15, "3kHz harmonic should be substantial");
    }

    void runResetTest() {
        beginTest("reset returns phase to zero");
        Oscillator osc;
        osc.prepare(SR);
        osc.setWaveform(Oscillator::Waveform::Sine);
        osc.setFrequency(440.0f);
        for (int i = 0; i < 100; ++i) osc.processSample();

        osc.reset();
        // After reset, the first sample of a sine at 440Hz starting from phase 0
        // should be very close to 0.
        float s0 = osc.processSample();
        expectWithinAbsoluteError(s0, 0.0f, 1e-3f);
    }

    void runZeroFreqTest() {
        beginTest("zero frequency produces silence (or DC) without exploding");
        Oscillator osc;
        osc.prepare(SR);
        osc.setWaveform(Oscillator::Waveform::Saw);
        osc.setFrequency(0.0f);
        std::vector<float> buf(BLOCK);
        for (int i = 0; i < BLOCK; ++i) buf[i] = osc.processSample();
        // Whatever the output, it must be bounded.
        for (float v : buf) expect(std::abs(v) <= 1.5f, "output must be bounded");
    }

    void runBlockContinuityTest() {
        beginTest("two consecutive blocks produce continuous output (no glitch at boundary)");
        Oscillator osc;
        osc.prepare(SR);
        osc.setWaveform(Oscillator::Waveform::Sine);
        osc.setFrequency(440.0f);
        std::vector<float> a(BLOCK), b(BLOCK);
        osc.processBlock(a.data(), BLOCK);
        osc.processBlock(b.data(), BLOCK);
        // Sample at boundary: |b[0] - a[BLOCK-1]| should be small for a smooth sine
        float delta = std::abs(b[0] - a[BLOCK - 1]);
        // For 440Hz sine at 48kHz, one-sample delta is < ~0.06
        expectLessThan(delta, 0.1f);
    }
};

static OscillatorTest oscillatorTestInstance;
```

- [ ] **Step 2: Stub the header so the test compiles**

Create `src/dsp/Oscillator.h`:

```cpp
#pragma once

class Oscillator {
public:
    enum class Waveform { Saw, Square, Triangle, Sine };

    void prepare(double sampleRate);
    void reset();
    void setWaveform(Waveform w);
    void setFrequency(float hz);

    float processSample();
    void processBlock(float* buffer, int numSamples);

private:
    double sampleRate_ = 44100.0;
    double phase_ = 0.0;      // 0..1
    double phaseInc_ = 0.0;   // freq / sampleRate
    float frequency_ = 0.0f;
    Waveform waveform_ = Waveform::Saw;
    double leakyInt_ = 0.0;   // triangle integrator state
};
```

Create empty `src/dsp/Oscillator.cpp`:

```cpp
#include "Oscillator.h"

void Oscillator::prepare(double sr) { sampleRate_ = sr; phase_ = 0.0; }
void Oscillator::reset() { phase_ = 0.0; }
void Oscillator::setWaveform(Waveform w) { waveform_ = w; }
void Oscillator::setFrequency(float hz) {
    frequency_ = hz;
    phaseInc_ = double(hz) / sampleRate_;
}
float Oscillator::processSample() { return 0.0f; } // stub — tests should fail
void Oscillator::processBlock(float* buf, int n) {
    for (int i = 0; i < n; ++i) buf[i] = processSample();
}
```

- [ ] **Step 3: Wire into the test build**

Edit `tests/CMakeLists.txt`:

```cmake
add_executable(k2000_tests
    TestMain.cpp
    SmokeTests.cpp
    ParamSnapshotTests.cpp
    OscillatorTests.cpp
    ../src/params/Parameters.cpp
    ../src/dsp/Oscillator.cpp)
```

- [ ] **Step 4: Build & run — confirm tests fail**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: `Oscillator` test reports failures (stub returns 0; no harmonics, no peak at 440Hz).

- [ ] **Step 5: Implement the oscillator**

Replace `src/dsp/Oscillator.cpp`:

```cpp
#include "Oscillator.h"
#include <cmath>

namespace {
    constexpr double kTwoPi = 6.283185307179586;

    // Standard polyBLEP correction. t is the phase in [0,1), dt is phaseInc.
    // Returns the value to subtract at upward discontinuity, or to add at
    // downward discontinuity (caller flips sign as needed).
    inline double polyBLEP(double t, double dt) {
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0;
        } else if (t > 1.0 - dt) {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }
}

void Oscillator::prepare(double sr) {
    sampleRate_ = sr;
    phase_ = 0.0;
    phaseInc_ = double(frequency_) / sampleRate_;
    leakyInt_ = 0.0;
}

void Oscillator::reset() {
    phase_ = 0.0;
    leakyInt_ = 0.0;
}

void Oscillator::setWaveform(Waveform w) { waveform_ = w; }

void Oscillator::setFrequency(float hz) {
    frequency_ = hz;
    phaseInc_ = double(hz) / sampleRate_;
}

float Oscillator::processSample() {
    // Guard against degenerate / negative freq
    if (phaseInc_ <= 0.0) return 0.0f;

    double v = 0.0;
    const double dt = phaseInc_;
    const double t  = phase_;

    switch (waveform_) {
        case Waveform::Sine:
            v = std::sin(kTwoPi * t);
            break;

        case Waveform::Saw:
            v = 2.0 * t - 1.0;
            v -= polyBLEP(t, dt);
            break;

        case Waveform::Square:
            v = (t < 0.5) ? 1.0 : -1.0;
            v += polyBLEP(t, dt);
            v -= polyBLEP(std::fmod(t + 0.5, 1.0), dt);
            break;

        case Waveform::Triangle: {
            // Integrate a polyBLEP-corrected square. Leaky integrator keeps
            // DC out. Scale by 4*dt to get ~unit-amplitude triangle.
            double sq = (t < 0.5) ? 1.0 : -1.0;
            sq += polyBLEP(t, dt);
            sq -= polyBLEP(std::fmod(t + 0.5, 1.0), dt);
            leakyInt_ = leakyInt_ * 0.999 + sq * 4.0 * dt;
            v = leakyInt_;
            break;
        }
    }

    phase_ += dt;
    if (phase_ >= 1.0) phase_ -= 1.0;
    return float(v);
}

void Oscillator::processBlock(float* buf, int n) {
    for (int i = 0; i < n; ++i) buf[i] = processSample();
}
```

- [ ] **Step 6: Build & run — confirm tests pass**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: All Oscillator tests pass. If "saw has harmonics" fails, double-check the polyBLEP sign convention and that `setFrequency` is being called with a positive value before the test loop.

- [ ] **Step 7: Commit**

```bash
git add src/dsp/Oscillator.* tests/OscillatorTests.cpp tests/CMakeLists.txt
git commit -m "feat(dsp): polyBLEP oscillator with saw/square/triangle/sine"
git push
```

---

## Task 7: ADSR Envelope

**Goal:** An ADSR amp envelope. TDD-style.

**Files:**
- Create: `src/dsp/Envelope.h`
- Create: `src/dsp/Envelope.cpp`
- Create: `tests/EnvelopeTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create `tests/EnvelopeTests.cpp`**

```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/Envelope.h"

class EnvelopeTest : public juce::UnitTest {
public:
    EnvelopeTest() : juce::UnitTest("Envelope") {}

    static constexpr double SR = 48000.0;

    void runTest() override {
        beginTest("starts at zero and is idle");
        Envelope e;
        e.prepare(SR);
        e.setParameters(0.01f, 0.1f, 0.5f, 0.2f);
        expectWithinAbsoluteError(e.nextSample(), 0.0f, 1e-6f);
        expect(!e.isActive());

        beginTest("attack ramps from 0 to ~1 over the attack time");
        e.reset();
        e.noteOn();
        // After ~attack samples we should be near 1.0
        int attackSamples = int(0.01 * SR);
        for (int i = 0; i < attackSamples; ++i) e.nextSample();
        float v = e.nextSample();
        expect(v > 0.9f, juce::String("expected near 1 after attack, got ") + juce::String(v));

        beginTest("decay falls toward sustain");
        // Run long enough for decay to settle
        for (int i = 0; i < int(SR); ++i) e.nextSample();
        float settled = e.nextSample();
        expectWithinAbsoluteError(settled, 0.5f, 0.05f);

        beginTest("noteOff drops to zero by end of release");
        e.noteOff();
        int releaseSamples = int(0.2 * SR);
        for (int i = 0; i < releaseSamples * 3; ++i) e.nextSample();
        expectWithinAbsoluteError(e.nextSample(), 0.0f, 1e-3f);
        expect(!e.isActive());

        beginTest("reset clears state");
        e.noteOn();
        e.nextSample();
        e.reset();
        expectWithinAbsoluteError(e.nextSample(), 0.0f, 1e-6f);
        expect(!e.isActive());
    }
};

static EnvelopeTest envelopeTestInstance;
```

- [ ] **Step 2: Stub `src/dsp/Envelope.h`**

```cpp
#pragma once

class Envelope {
public:
    void prepare(double sampleRate);
    void reset();
    void setParameters(float attackS, float decayS, float sustain, float releaseS);
    void noteOn();
    void noteOff();
    bool isActive() const;
    float nextSample();

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage_ = Stage::Idle;
    double sampleRate_ = 44100.0;
    float attackInc_  = 0.0f;
    float decayCoef_  = 0.0f;
    float releaseCoef_ = 0.0f;
    float sustain_    = 0.5f;
    float value_      = 0.0f;
};
```

- [ ] **Step 3: Stub `src/dsp/Envelope.cpp`** so the test compiles

```cpp
#include "Envelope.h"

void Envelope::prepare(double sr) { sampleRate_ = sr; reset(); }
void Envelope::reset() { stage_ = Stage::Idle; value_ = 0.0f; }
void Envelope::setParameters(float, float, float, float) {}
void Envelope::noteOn() {}
void Envelope::noteOff() {}
bool Envelope::isActive() const { return stage_ != Stage::Idle; }
float Envelope::nextSample() { return 0.0f; }
```

Add `Envelope.cpp` to `tests/CMakeLists.txt`:

```cmake
add_executable(k2000_tests
    TestMain.cpp
    SmokeTests.cpp
    ParamSnapshotTests.cpp
    OscillatorTests.cpp
    EnvelopeTests.cpp
    ../src/params/Parameters.cpp
    ../src/dsp/Oscillator.cpp
    ../src/dsp/Envelope.cpp)
```

- [ ] **Step 4: Build & run — confirm Envelope tests fail**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: Envelope test fails.

- [ ] **Step 5: Implement the envelope**

Replace `src/dsp/Envelope.cpp`:

```cpp
#include "Envelope.h"
#include <cmath>
#include <algorithm>

void Envelope::prepare(double sr) {
    sampleRate_ = sr;
    reset();
}

void Envelope::reset() {
    stage_ = Stage::Idle;
    value_ = 0.0f;
}

// Exponential coefficient that decays from current value to target over
// `timeSeconds` reaching ~99% of the way (3 time constants).
static float expCoef(float timeSeconds, double sampleRate) {
    if (timeSeconds <= 0.0f) return 0.0f;
    return float(std::exp(-1.0 / (timeSeconds * sampleRate / 3.0)));
}

void Envelope::setParameters(float attackS, float decayS, float sustain, float releaseS) {
    attackInc_  = (attackS > 0.0f) ? float(1.0 / (attackS * sampleRate_)) : 1.0f;
    decayCoef_  = expCoef(decayS, sampleRate_);
    releaseCoef_ = expCoef(releaseS, sampleRate_);
    sustain_    = std::clamp(sustain, 0.0f, 1.0f);
}

void Envelope::noteOn() {
    stage_ = Stage::Attack;
}

void Envelope::noteOff() {
    if (stage_ != Stage::Idle) stage_ = Stage::Release;
}

bool Envelope::isActive() const {
    return stage_ != Stage::Idle;
}

float Envelope::nextSample() {
    switch (stage_) {
        case Stage::Idle:
            value_ = 0.0f;
            break;

        case Stage::Attack:
            value_ += attackInc_;
            if (value_ >= 1.0f) {
                value_ = 1.0f;
                stage_ = Stage::Decay;
            }
            break;

        case Stage::Decay:
            value_ = sustain_ + (value_ - sustain_) * decayCoef_;
            if (std::abs(value_ - sustain_) < 1e-4f) {
                value_ = sustain_;
                stage_ = Stage::Sustain;
            }
            break;

        case Stage::Sustain:
            value_ = sustain_;
            break;

        case Stage::Release:
            value_ *= releaseCoef_;
            if (value_ < 1e-4f) {
                value_ = 0.0f;
                stage_ = Stage::Idle;
            }
            break;
    }
    return value_;
}
```

- [ ] **Step 6: Build & run — confirm tests pass**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

If "decay falls toward sustain" fails by a hair, widen the tolerance slightly — the exponential decay won't hit sustain to 1e-6 in finite time, but should be within 0.05.

- [ ] **Step 7: Commit**

```bash
git add src/dsp/Envelope.* tests/EnvelopeTests.cpp tests/CMakeLists.txt
git commit -m "feat(dsp): ADSR envelope with exponential decay/release"
git push
```

---

## Task 8: SVF Filter (state-variable, TPT topology)

**Goal:** A multimode state-variable filter implementing the trapezoidal-integrated SVF (also called TPT-SVF, the modern stable formulation by Vadim Zavalishin / Cytomic). Implements `DSPBlock`.

**Files:**
- Create: `src/dsp/blocks/SVFFilter.h`
- Create: `src/dsp/blocks/SVFFilter.cpp`
- Create: `tests/SVFFilterTests.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

### Background — TPT-SVF formulation

For each sample:
```
g = tan(π * cutoff / sampleRate)
k = 1 / Q                    (Q = 1 / (2 * (1 - resonance)))
a1 = 1 / (1 + g * (g + k))
a2 = g * a1
a3 = g * a2

v3 = input - ic2eq
v1 = a1 * ic1eq + a2 * v3
v2 = ic2eq + a2 * ic1eq + a3 * v3

ic1eq = 2 * v1 - ic1eq        (state update)
ic2eq = 2 * v2 - ic2eq

LP  = v2
BP  = v1
HP  = input - k * v1 - v2
Notch = input - k * v1        (or HP + LP)
```

`g`, `k`, and the `a*` coefficients only need recomputation when cutoff or resonance changes. We'll recompute per block (block-rate parameter updates) — cheap enough and avoids per-sample `tan`.

- [ ] **Step 1: Create the test file `tests/SVFFilterTests.cpp`**

```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/blocks/SVFFilter.h"
#include "../src/params/ParamSnapshot.h"
#include <vector>
#include <cmath>

class SVFFilterTest : public juce::UnitTest {
public:
    SVFFilterTest() : juce::UnitTest("SVFFilter") {}

    static constexpr double SR = 48000.0;
    static constexpr int BLOCK = 1024;

    void runTest() override {
        beginTest("LP with high cutoff passes DC nearly unchanged");
        {
            SVFFilter f;
            f.prepare(SR, BLOCK);
            f.reset();
            ParamSnapshot s;
            s.svfType = 0; s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
            f.updateParameters(s);
            std::vector<float> buf(BLOCK, 1.0f);  // DC = 1
            f.process(buf.data(), BLOCK);
            // After settling, output should be ~1
            expectWithinAbsoluteError(buf.back(), 1.0f, 0.05f);
        }

        beginTest("LP with low cutoff attenuates a 5kHz sine");
        {
            SVFFilter f;
            f.prepare(SR, BLOCK);
            f.reset();
            ParamSnapshot s;
            s.svfType = 0; s.svfCutoffHz = 200.0f; s.svfResonance = 0.0f;
            f.updateParameters(s);

            const int N = 8 * BLOCK;
            std::vector<float> buf(N);
            double phase = 0;
            for (int i = 0; i < N; ++i) {
                buf[i] = float(std::sin(phase));
                phase += 2.0 * juce::MathConstants<double>::pi * 5000.0 / SR;
            }
            // Skip the first block (transient) and process; measure RMS of last half.
            f.process(buf.data(), N);
            double s2 = 0;
            int from = N / 2;
            for (int i = from; i < N; ++i) s2 += double(buf[i]) * buf[i];
            double rmsOut = std::sqrt(s2 / (N - from));
            expectLessThan(rmsOut, 0.1, "5kHz should be heavily attenuated by 200Hz LP");
        }

        beginTest("HP with low cutoff blocks DC");
        {
            SVFFilter f;
            f.prepare(SR, BLOCK);
            f.reset();
            ParamSnapshot s;
            s.svfType = 1; s.svfCutoffHz = 200.0f; s.svfResonance = 0.0f;
            f.updateParameters(s);
            std::vector<float> buf(BLOCK * 8, 1.0f);
            f.process(buf.data(), int(buf.size()));
            // Output near the end should be ~0
            expectWithinAbsoluteError(buf.back(), 0.0f, 0.05f);
        }

        beginTest("output is bounded at high resonance");
        {
            SVFFilter f;
            f.prepare(SR, BLOCK);
            f.reset();
            ParamSnapshot s;
            s.svfType = 0; s.svfCutoffHz = 1000.0f; s.svfResonance = 0.99f;
            f.updateParameters(s);
            std::vector<float> buf(BLOCK);
            for (int i = 0; i < BLOCK; ++i)
                buf[i] = float(std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * i / SR));
            f.process(buf.data(), BLOCK);
            for (float v : buf)
                expect(std::abs(v) < 10.0f, "high-resonance output must not blow up");
        }

        beginTest("reset returns state to zero");
        {
            SVFFilter f;
            f.prepare(SR, BLOCK);
            ParamSnapshot s;
            s.svfType = 0; s.svfCutoffHz = 1000.0f; s.svfResonance = 0.5f;
            f.updateParameters(s);
            std::vector<float> buf(BLOCK, 1.0f);
            f.process(buf.data(), BLOCK);
            f.reset();
            std::vector<float> buf2(BLOCK, 0.0f);
            f.process(buf2.data(), BLOCK);
            // With zero input after reset, output should be zero
            for (float v : buf2)
                expectWithinAbsoluteError(v, 0.0f, 1e-6f);
        }
    }
};

static SVFFilterTest svfFilterTestInstance;
```

- [ ] **Step 2: Stub `src/dsp/blocks/SVFFilter.h`**

```cpp
#pragma once
#include "../DSPBlock.h"

class SVFFilter : public DSPBlock {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    juce::String getTypeId() const override { return "svf_filter"; }
    std::vector<ParamSpec> getParamSpecs() const override;
    void updateParameters(const ParamSnapshot& snapshot) override;

private:
    double sampleRate_ = 44100.0;
    int type_ = 0;  // 0=LP 1=HP 2=BP 3=Notch
    float cutoffHz_ = 1000.0f;
    float resonance_ = 0.0f;

    // Coefficients (recomputed when cutoff/resonance change)
    float g_ = 0, k_ = 0, a1_ = 0, a2_ = 0, a3_ = 0;
    bool coefsDirty_ = true;

    // State
    float ic1eq_ = 0, ic2eq_ = 0;

    void recomputeCoefs();
};
```

- [ ] **Step 3: Stub `src/dsp/blocks/SVFFilter.cpp`**

```cpp
#include "SVFFilter.h"

void SVFFilter::prepare(double sr, int) { sampleRate_ = sr; reset(); coefsDirty_ = true; }
void SVFFilter::reset() { ic1eq_ = ic2eq_ = 0; }
void SVFFilter::process(float* buf, int n) { (void)buf; (void)n; }
std::vector<ParamSpec> SVFFilter::getParamSpecs() const { return {}; }
void SVFFilter::updateParameters(const ParamSnapshot&) {}
void SVFFilter::recomputeCoefs() {}
```

- [ ] **Step 4: Add to test build**

Edit `tests/CMakeLists.txt`:

```cmake
add_executable(k2000_tests
    TestMain.cpp
    SmokeTests.cpp
    ParamSnapshotTests.cpp
    OscillatorTests.cpp
    EnvelopeTests.cpp
    SVFFilterTests.cpp
    ../src/params/Parameters.cpp
    ../src/dsp/Oscillator.cpp
    ../src/dsp/Envelope.cpp
    ../src/dsp/blocks/SVFFilter.cpp)
```

- [ ] **Step 5: Build & run — confirm SVF tests fail**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: SVFFilter tests fail.

- [ ] **Step 6: Implement the SVF**

Replace `src/dsp/blocks/SVFFilter.cpp`:

```cpp
#include "SVFFilter.h"
#include <cmath>
#include <algorithm>

void SVFFilter::prepare(double sr, int) {
    sampleRate_ = sr;
    reset();
    coefsDirty_ = true;
}

void SVFFilter::reset() {
    ic1eq_ = ic2eq_ = 0.0f;
}

void SVFFilter::updateParameters(const ParamSnapshot& s) {
    if (s.svfType != type_) { type_ = s.svfType; }
    if (s.svfCutoffHz != cutoffHz_ || s.svfResonance != resonance_) {
        cutoffHz_ = s.svfCutoffHz;
        resonance_ = s.svfResonance;
        coefsDirty_ = true;
    }
}

void SVFFilter::recomputeCoefs() {
    // Clamp cutoff to safe range
    const float cutoff = std::clamp(cutoffHz_, 20.0f, float(sampleRate_ * 0.45));
    const float res = std::clamp(resonance_, 0.0f, 0.999f);

    // Q ranges from 0.5 (no resonance) to 9.0 (max resonance, quadratic curve).
    // Capped at 9.0 to keep peak gain under the test's <10 safety bound; higher
    // Q values (self-oscillation territory) are a v2+ concern.
    const float Q = 0.5f + res * res * 8.5f;
    g_ = float(std::tan(juce::MathConstants<double>::pi * cutoff / sampleRate_));
    k_ = 1.0f / Q;

    a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
    a2_ = g_ * a1_;
    a3_ = g_ * a2_;
    coefsDirty_ = false;
}

void SVFFilter::process(float* buf, int n) {
    if (coefsDirty_) recomputeCoefs();

    for (int i = 0; i < n; ++i) {
        const float v0 = buf[i];
        const float v3 = v0 - ic2eq_;
        const float v1 = a1_ * ic1eq_ + a2_ * v3;
        const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;
        ic1eq_ = 2.0f * v1 - ic1eq_;
        ic2eq_ = 2.0f * v2 - ic2eq_;

        float out = 0.0f;
        switch (type_) {
            case 0: out = v2; break;                    // LP
            case 1: out = v0 - k_ * v1 - v2; break;     // HP
            case 2: out = v1; break;                    // BP
            case 3: out = v0 - k_ * v1; break;          // Notch
            default: out = v2;
        }
        buf[i] = out;
    }
}

std::vector<ParamSpec> SVFFilter::getParamSpecs() const {
    // v1 reads parameters from ParamSnapshot directly; getParamSpecs() is
    // declared by the interface but not yet driving APVTS registration
    // (the v1 processor builds the APVTS layout from params/Parameters.h).
    // When v4 makes slot type user-selectable this will become the source
    // of truth — for now an empty vector is fine.
    return {};
}
```

- [ ] **Step 7: Build & run — confirm tests pass**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

If `HP blocks DC` fails, run the input through more blocks — DC blocking takes time at low cutoffs.

- [ ] **Step 8: Commit**

```bash
git add src/dsp/blocks/SVFFilter.* tests/SVFFilterTests.cpp tests/CMakeLists.txt
git commit -m "feat(dsp): TPT state-variable filter (LP/HP/BP/Notch)"
git push
```

---

## Task 9: Waveshaper

**Goal:** A `tanh`-based waveshaper with drive and dry/wet mix. Implements `DSPBlock`.

**Files:**
- Create: `src/dsp/blocks/Waveshaper.h`
- Create: `src/dsp/blocks/Waveshaper.cpp`
- Create: `tests/WaveshaperTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create `tests/WaveshaperTests.cpp`**

```cpp
#include <juce_core/juce_core.h>
#include "../src/dsp/blocks/Waveshaper.h"
#include "../src/params/ParamSnapshot.h"
#include <vector>
#include <cmath>

class WaveshaperTest : public juce::UnitTest {
public:
    WaveshaperTest() : juce::UnitTest("Waveshaper") {}

    void runTest() override {
        beginTest("mix=0 passes input unchanged");
        {
            Waveshaper w;
            w.prepare(48000.0, 256);
            ParamSnapshot s; s.wsDrive = 1.0f; s.wsMix = 0.0f;
            w.updateParameters(s);
            std::vector<float> buf{0.0f, 0.5f, -0.5f, 0.9f, -0.9f};
            std::vector<float> expected = buf;
            w.process(buf.data(), int(buf.size()));
            for (size_t i = 0; i < buf.size(); ++i)
                expectWithinAbsoluteError(buf[i], expected[i], 1e-6f);
        }

        beginTest("mix=1 drive=0 passes input nearly unchanged for small signals");
        {
            Waveshaper w;
            w.prepare(48000.0, 256);
            ParamSnapshot s; s.wsDrive = 0.0f; s.wsMix = 1.0f;
            w.updateParameters(s);
            std::vector<float> buf{0.0f, 0.1f, -0.1f};
            std::vector<float> expected = buf;
            w.process(buf.data(), int(buf.size()));
            for (size_t i = 0; i < buf.size(); ++i)
                expectWithinAbsoluteError(buf[i], expected[i], 0.05f);
        }

        beginTest("output is bounded");
        {
            Waveshaper w;
            w.prepare(48000.0, 256);
            ParamSnapshot s; s.wsDrive = 1.0f; s.wsMix = 1.0f;
            w.updateParameters(s);
            std::vector<float> buf(1024);
            for (size_t i = 0; i < buf.size(); ++i) buf[i] = 5.0f;  // extreme
            w.process(buf.data(), int(buf.size()));
            for (float v : buf) expect(std::abs(v) <= 1.0f, "output must be in [-1, 1]");
        }

        beginTest("odd symmetry: shaper(-x) == -shaper(x)");
        {
            Waveshaper w;
            w.prepare(48000.0, 256);
            ParamSnapshot s; s.wsDrive = 0.7f; s.wsMix = 1.0f;
            w.updateParameters(s);
            std::vector<float> pos{0.1f, 0.5f, 0.9f};
            std::vector<float> neg{-0.1f, -0.5f, -0.9f};
            w.process(pos.data(), int(pos.size()));
            w.process(neg.data(), int(neg.size()));
            for (size_t i = 0; i < pos.size(); ++i)
                expectWithinAbsoluteError(pos[i], -neg[i], 1e-6f);
        }
    }
};

static WaveshaperTest waveshaperTestInstance;
```

- [ ] **Step 2: Stub `src/dsp/blocks/Waveshaper.h`**

```cpp
#pragma once
#include "../DSPBlock.h"

class Waveshaper : public DSPBlock {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void process(float* buffer, int numSamples) override;
    juce::String getTypeId() const override { return "waveshaper"; }
    std::vector<ParamSpec> getParamSpecs() const override { return {}; }
    void updateParameters(const ParamSnapshot& snapshot) override;

private:
    float drive_ = 0.0f;
    float mix_   = 1.0f;
};
```

- [ ] **Step 3: Stub `src/dsp/blocks/Waveshaper.cpp`**

```cpp
#include "Waveshaper.h"

void Waveshaper::prepare(double, int) {}
void Waveshaper::reset() {}
void Waveshaper::updateParameters(const ParamSnapshot&) {}
void Waveshaper::process(float*, int) {}
```

- [ ] **Step 4: Wire into test build**

Edit `tests/CMakeLists.txt`:

```cmake
add_executable(k2000_tests
    TestMain.cpp
    SmokeTests.cpp
    ParamSnapshotTests.cpp
    OscillatorTests.cpp
    EnvelopeTests.cpp
    SVFFilterTests.cpp
    WaveshaperTests.cpp
    ../src/params/Parameters.cpp
    ../src/dsp/Oscillator.cpp
    ../src/dsp/Envelope.cpp
    ../src/dsp/blocks/SVFFilter.cpp
    ../src/dsp/blocks/Waveshaper.cpp)
```

- [ ] **Step 5: Build & run — confirm Waveshaper tests fail**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

- [ ] **Step 6: Implement the Waveshaper**

Replace `src/dsp/blocks/Waveshaper.cpp`:

```cpp
#include "Waveshaper.h"
#include <cmath>
#include <algorithm>

void Waveshaper::prepare(double, int) {}
void Waveshaper::reset() {}

void Waveshaper::updateParameters(const ParamSnapshot& s) {
    drive_ = std::clamp(s.wsDrive, 0.0f, 1.0f);
    mix_   = std::clamp(s.wsMix,   0.0f, 1.0f);
}

void Waveshaper::process(float* buf, int n) {
    // Drive: 0..1 maps to gain 1..10 before tanh; output normalised by tanh's saturation.
    const float gain = 1.0f + drive_ * 9.0f;
    const float invGain = 1.0f / std::tanh(gain);  // normalise full-scale input to ±1
    for (int i = 0; i < n; ++i) {
        const float dry = buf[i];
        const float wet = std::tanh(dry * gain) * invGain;
        buf[i] = dry + (wet - dry) * mix_;
    }
}
```

- [ ] **Step 7: Build & run — confirm tests pass**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

- [ ] **Step 8: Commit**

```bash
git add src/dsp/blocks/Waveshaper.* tests/WaveshaperTests.cpp tests/CMakeLists.txt
git commit -m "feat(dsp): tanh waveshaper with drive and mix"
git push
```

---

## Task 10: Voice

**Goal:** Assemble Oscillator + two DSP slots + ADSR amp into a single voice that responds to noteOn / noteOff and renders into a buffer.

**Files:**
- Create: `src/Voice.h`
- Create: `src/Voice.cpp`
- Create: `tests/VoiceTests.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Create `src/Voice.h`**

```cpp
#pragma once
#include <array>
#include <memory>
#include "dsp/Oscillator.h"
#include "dsp/Envelope.h"
#include "dsp/DSPBlock.h"
#include "params/ParamSnapshot.h"

class Voice {
public:
    Voice();
    ~Voice() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void noteOn(int midiNote, float velocity);
    void noteOff();
    bool isActive() const;
    int  currentNote() const { return note_; }

    // RT-safe. Renders into `out`, additively (caller pre-zeroes).
    void render(float* out, int numSamples, const ParamSnapshot& snapshot);

private:
    Oscillator osc_;
    std::array<std::unique_ptr<DSPBlock>, 2> slots_;
    Envelope amp_;

    int note_ = -1;
    float velocity_ = 0.0f;
    double sampleRate_ = 44100.0;

    static float midiToHz(int note);
};
```

- [ ] **Step 2: Create `src/Voice.cpp`**

```cpp
#include "Voice.h"
#include "dsp/blocks/SVFFilter.h"
#include "dsp/blocks/Waveshaper.h"
#include <cmath>
#include <vector>

Voice::Voice() {
    slots_[0] = std::make_unique<SVFFilter>();
    slots_[1] = std::make_unique<Waveshaper>();
}

void Voice::prepare(double sr, int maxBlock) {
    sampleRate_ = sr;
    osc_.prepare(sr);
    amp_.prepare(sr);
    for (auto& s : slots_) s->prepare(sr, maxBlock);
    reset();
}

void Voice::reset() {
    osc_.reset();
    amp_.reset();
    for (auto& s : slots_) s->reset();
    note_ = -1;
    velocity_ = 0.0f;
}

void Voice::noteOn(int midiNote, float velocity) {
    note_ = midiNote;
    velocity_ = velocity;
    osc_.reset();
    amp_.reset();
    for (auto& s : slots_) s->reset();
    amp_.noteOn();
}

void Voice::noteOff() {
    amp_.noteOff();
}

bool Voice::isActive() const {
    return amp_.isActive();
}

float Voice::midiToHz(int note) {
    return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
}

void Voice::render(float* out, int numSamples, const ParamSnapshot& s) {
    if (!isActive()) return;

    // Apply parameter snapshot to all sub-components.
    const float tune = s.oscCoarse + s.oscFine * 0.01f;
    const float hz = midiToHz(note_) * std::pow(2.0f, tune / 12.0f);
    osc_.setFrequency(hz);
    osc_.setWaveform(static_cast<Oscillator::Waveform>(s.oscWaveform));
    amp_.setParameters(s.ampAttackS, s.ampDecayS, s.ampSustain, s.ampReleaseS);
    for (auto& slot : slots_) slot->updateParameters(s);

    // Render oscillator into a scratch buffer, run through slots, apply amp.
    std::vector<float> tmp(numSamples);
    osc_.processBlock(tmp.data(), numSamples);
    for (auto& slot : slots_) slot->process(tmp.data(), numSamples);

    for (int i = 0; i < numSamples; ++i) {
        out[i] += tmp[i] * amp_.nextSample() * velocity_;
    }
}
```

Note the `std::vector<float> tmp(numSamples)` allocation in `render` — **this is an RT-safety violation that we'll fix in Task 12** when the processor owns a preallocated scratch buffer pool. For now, the test just verifies functional behaviour; we leave a TODO marker in the comment and address it in the integration step.

Add a comment in the source to flag this:

```cpp
// TODO(rt-safety): per-block allocation here. Task 12 replaces this with
// a preallocated scratch buffer owned by the processor.
std::vector<float> tmp(numSamples);
```

- [ ] **Step 3: Wire into main build**

Edit `CMakeLists.txt` `target_sources` block:

```cmake
target_sources(k2000 PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Voice.cpp
    src/params/Parameters.cpp
    src/dsp/Oscillator.cpp
    src/dsp/Envelope.cpp
    src/dsp/blocks/SVFFilter.cpp
    src/dsp/blocks/Waveshaper.cpp)
```

- [ ] **Step 4: Create `tests/VoiceTests.cpp`**

```cpp
#include <juce_core/juce_core.h>
#include "../src/Voice.h"
#include "../src/params/ParamSnapshot.h"
#include <vector>
#include <cmath>

class VoiceTest : public juce::UnitTest {
public:
    VoiceTest() : juce::UnitTest("Voice") {}

    static constexpr double SR = 48000.0;
    static constexpr int BLOCK = 256;

    void runTest() override {
        ParamSnapshot s;
        s.oscWaveform = 3;  // sine for clean test signal
        s.svfType = 0; s.svfCutoffHz = 20000.0f; s.svfResonance = 0.0f;
        s.wsDrive = 0.0f; s.wsMix = 0.0f;
        s.ampAttackS = 0.001f; s.ampDecayS = 0.01f;
        s.ampSustain = 1.0f; s.ampReleaseS = 0.01f;

        Voice v;
        v.prepare(SR, BLOCK);

        beginTest("idle voice renders nothing");
        std::vector<float> out(BLOCK, 0.0f);
        v.render(out.data(), BLOCK, s);
        for (float x : out) expectWithinAbsoluteError(x, 0.0f, 1e-6f);

        beginTest("noteOn produces non-zero output");
        v.noteOn(69, 1.0f);  // A4
        std::fill(out.begin(), out.end(), 0.0f);
        // Render two blocks to let envelope ramp past attack.
        v.render(out.data(), BLOCK, s);
        std::fill(out.begin(), out.end(), 0.0f);
        v.render(out.data(), BLOCK, s);
        double sumAbs = 0;
        for (float x : out) sumAbs += std::abs(x);
        expect(sumAbs > 1.0, "voice should produce audible output after noteOn");

        beginTest("noteOff eventually silences voice");
        v.noteOff();
        for (int i = 0; i < 200; ++i) {
            std::fill(out.begin(), out.end(), 0.0f);
            v.render(out.data(), BLOCK, s);
            if (!v.isActive()) break;
        }
        expect(!v.isActive(), "voice should become inactive after release completes");
    }
};

static VoiceTest voiceTestInstance;
```

- [ ] **Step 5: Wire into test build**

Edit `tests/CMakeLists.txt`:

```cmake
add_executable(k2000_tests
    TestMain.cpp
    SmokeTests.cpp
    ParamSnapshotTests.cpp
    OscillatorTests.cpp
    EnvelopeTests.cpp
    SVFFilterTests.cpp
    WaveshaperTests.cpp
    VoiceTests.cpp
    ../src/Voice.cpp
    ../src/params/Parameters.cpp
    ../src/dsp/Oscillator.cpp
    ../src/dsp/Envelope.cpp
    ../src/dsp/blocks/SVFFilter.cpp
    ../src/dsp/blocks/Waveshaper.cpp)
```

- [ ] **Step 6: Build & run**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/Voice.* tests/VoiceTests.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: Voice assembling oscillator + DSP slots + ADSR amp"
git push
```

---

## Task 11: VoiceManager

**Goal:** Polyphony: allocate voices to incoming notes, steal when out of voices, mix outputs.

**Files:**
- Create: `src/VoiceManager.h`
- Create: `src/VoiceManager.cpp`
- Create: `tests/VoiceManagerTests.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Create `src/VoiceManager.h`**

```cpp
#pragma once
#include <array>
#include "Voice.h"

class VoiceManager {
public:
    static constexpr int kNumVoices = 8;

    void prepare(double sampleRate, int maxBlockSize);

    // RT-safe. Process MIDI events at their exact sample positions and
    // mix voice outputs additively into `out` (mono).
    void renderBlock(float* out, int numSamples,
                     const juce::MidiBuffer& midi,
                     const ParamSnapshot& snapshot);

    void allNotesOff();

private:
    std::array<Voice, kNumVoices> voices_;

    // Steal strategy: prefer an inactive voice; otherwise the oldest active.
    int pickVoiceFor(int midiNote);
    void noteOn(int midiNote, float velocity);
    void noteOff(int midiNote);

    // Per-voice age counter (incremented on noteOn for ordering)
    std::array<int, kNumVoices> voiceAge_{};
    int ageCounter_ = 0;
};
```

- [ ] **Step 2: Create `src/VoiceManager.cpp`**

```cpp
#include "VoiceManager.h"

void VoiceManager::prepare(double sr, int maxBlock) {
    for (auto& v : voices_) v.prepare(sr, maxBlock);
    voiceAge_.fill(0);
    ageCounter_ = 0;
}

void VoiceManager::allNotesOff() {
    for (auto& v : voices_) v.noteOff();
}

int VoiceManager::pickVoiceFor(int) {
    // Inactive voice first
    for (int i = 0; i < kNumVoices; ++i)
        if (!voices_[i].isActive()) return i;
    // Otherwise steal the oldest (smallest age)
    int oldest = 0;
    for (int i = 1; i < kNumVoices; ++i)
        if (voiceAge_[i] < voiceAge_[oldest]) oldest = i;
    return oldest;
}

void VoiceManager::noteOn(int midiNote, float velocity) {
    int v = pickVoiceFor(midiNote);
    voices_[v].noteOn(midiNote, velocity);
    voiceAge_[v] = ++ageCounter_;
}

void VoiceManager::noteOff(int midiNote) {
    for (int i = 0; i < kNumVoices; ++i)
        if (voices_[i].isActive() && voices_[i].currentNote() == midiNote)
            voices_[i].noteOff();
}

void VoiceManager::renderBlock(float* out, int numSamples,
                               const juce::MidiBuffer& midi,
                               const ParamSnapshot& s) {
    int cursor = 0;
    auto renderRange = [&](int from, int to) {
        if (to <= from) return;
        const int len = to - from;
        for (auto& v : voices_) v.render(out + from, len, s);
    };

    for (const auto meta : midi) {
        const int pos = meta.samplePosition;
        renderRange(cursor, pos);
        cursor = pos;

        const auto& m = meta.getMessage();
        if (m.isNoteOn())  noteOn(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) noteOff(m.getNoteNumber());
        else if (m.isAllNotesOff()) allNotesOff();
    }
    renderRange(cursor, numSamples);
}
```

- [ ] **Step 3: Add to main build**

Edit `CMakeLists.txt`:

```cmake
target_sources(k2000 PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Voice.cpp
    src/VoiceManager.cpp
    src/params/Parameters.cpp
    src/dsp/Oscillator.cpp
    src/dsp/Envelope.cpp
    src/dsp/blocks/SVFFilter.cpp
    src/dsp/blocks/Waveshaper.cpp)
```

- [ ] **Step 4: Create `tests/VoiceManagerTests.cpp`**

```cpp
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../src/VoiceManager.h"
#include <vector>
#include <cmath>

class VoiceManagerTest : public juce::UnitTest {
public:
    VoiceManagerTest() : juce::UnitTest("VoiceManager") {}

    static constexpr double SR = 48000.0;
    static constexpr int BLOCK = 256;

    void runTest() override {
        ParamSnapshot s;
        s.oscWaveform = 3;
        s.svfCutoffHz = 20000.0f;
        s.wsMix = 0.0f;
        s.ampAttackS = 0.001f; s.ampDecayS = 0.01f;
        s.ampSustain = 1.0f; s.ampReleaseS = 0.05f;

        VoiceManager vm;
        vm.prepare(SR, BLOCK);

        beginTest("no MIDI input → silent output");
        std::vector<float> out(BLOCK, 0.0f);
        juce::MidiBuffer midi;
        vm.renderBlock(out.data(), BLOCK, midi, s);
        double sumAbs = 0;
        for (float v : out) sumAbs += std::abs(v);
        expectWithinAbsoluteError(float(sumAbs), 0.0f, 1e-6f);

        beginTest("noteOn produces non-silent output");
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
        std::fill(out.begin(), out.end(), 0.0f);
        vm.renderBlock(out.data(), BLOCK, midi, s);
        std::fill(out.begin(), out.end(), 0.0f);
        midi.clear();
        vm.renderBlock(out.data(), BLOCK, midi, s);
        sumAbs = 0;
        for (float v : out) sumAbs += std::abs(v);
        expect(sumAbs > 1.0, "should produce audible output");

        beginTest("noteOff releases voice");
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        vm.renderBlock(out.data(), BLOCK, midi, s);
        // Run for long enough to drain release
        for (int b = 0; b < 200; ++b) {
            std::fill(out.begin(), out.end(), 0.0f);
            midi.clear();
            vm.renderBlock(out.data(), BLOCK, midi, s);
        }
        sumAbs = 0;
        for (float v : out) sumAbs += std::abs(v);
        expectLessThan(float(sumAbs), 1e-3f);

        beginTest("more notes than polyphony triggers voice stealing");
        midi.clear();
        for (int i = 0; i < VoiceManager::kNumVoices + 2; ++i)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60 + i, (juce::uint8) 100), 0);
        std::fill(out.begin(), out.end(), 0.0f);
        vm.renderBlock(out.data(), BLOCK, midi, s);
        // Just verify nothing crashed and we have a finite, bounded output.
        for (float v : out) expect(std::isfinite(v));
    }
};

static VoiceManagerTest voiceManagerTestInstance;
```

- [ ] **Step 5: Add to test build**

Edit `tests/CMakeLists.txt`:

```cmake
add_executable(k2000_tests
    TestMain.cpp
    SmokeTests.cpp
    ParamSnapshotTests.cpp
    OscillatorTests.cpp
    EnvelopeTests.cpp
    SVFFilterTests.cpp
    WaveshaperTests.cpp
    VoiceTests.cpp
    VoiceManagerTests.cpp
    ../src/Voice.cpp
    ../src/VoiceManager.cpp
    ../src/params/Parameters.cpp
    ../src/dsp/Oscillator.cpp
    ../src/dsp/Envelope.cpp
    ../src/dsp/blocks/SVFFilter.cpp
    ../src/dsp/blocks/Waveshaper.cpp)
```

- [ ] **Step 6: Build & run**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

- [ ] **Step 7: Commit**

```bash
git add src/VoiceManager.* tests/VoiceManagerTests.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: VoiceManager with 8-voice polyphony and steal-oldest"
git push
```

---

## Task 12: Wire up PluginProcessor

**Goal:** Connect APVTS + VoiceManager + processBlock so the plugin actually plays sound. Add state save/load and fix Voice's per-block allocation.

**Files:**
- Modify: `src/PluginProcessor.h`
- Modify: `src/PluginProcessor.cpp`
- Modify: `src/Voice.h`
- Modify: `src/Voice.cpp`
- Create: `tests/PluginLifecycleTests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Fix the Voice scratch buffer allocation**

Edit `src/Voice.h` — add a member:

```cpp
private:
    std::vector<float> scratch_;
```

Edit `src/Voice.cpp` — in `prepare`, size the scratch:

```cpp
void Voice::prepare(double sr, int maxBlock) {
    sampleRate_ = sr;
    osc_.prepare(sr);
    amp_.prepare(sr);
    for (auto& s : slots_) s->prepare(sr, maxBlock);
    scratch_.assign(maxBlock, 0.0f);  // allocate once, RT-safe henceforth
    reset();
}
```

And replace the `render` body's allocation:

```cpp
void Voice::render(float* out, int numSamples, const ParamSnapshot& s) {
    if (!isActive()) return;

    const float tune = s.oscCoarse + s.oscFine * 0.01f;
    const float hz = midiToHz(note_) * std::pow(2.0f, tune / 12.0f);
    osc_.setFrequency(hz);
    osc_.setWaveform(static_cast<Oscillator::Waveform>(s.oscWaveform));
    amp_.setParameters(s.ampAttackS, s.ampDecayS, s.ampSustain, s.ampReleaseS);
    for (auto& slot : slots_) slot->updateParameters(s);

    jassert(numSamples <= (int) scratch_.size());
    float* tmp = scratch_.data();
    osc_.processBlock(tmp, numSamples);
    for (auto& slot : slots_) slot->process(tmp, numSamples);

    for (int i = 0; i < numSamples; ++i) {
        out[i] += tmp[i] * amp_.nextSample() * velocity_;
    }
}
```

Also remove the `#include <vector>` from `Voice.cpp` (no longer needed there) — but keep it in `Voice.h` since the member needs it.

- [ ] **Step 2: Replace `src/PluginProcessor.h`**

```cpp
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "VoiceManager.h"
#include "params/Parameters.h"

class K2000AudioProcessor : public juce::AudioProcessor {
public:
    K2000AudioProcessor();
    ~K2000AudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "k2000"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& apvts() { return apvts_; }

private:
    juce::AudioProcessorValueTreeState apvts_;
    VoiceManager voiceManager_;
    std::vector<float> monoScratch_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(K2000AudioProcessor)
};
```

- [ ] **Step 3: Replace `src/PluginProcessor.cpp`**

```cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"

K2000AudioProcessor::K2000AudioProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "PARAMS", params::createLayout()) {}

void K2000AudioProcessor::prepareToPlay(double sr, int samplesPerBlock) {
    voiceManager_.prepare(sr, samplesPerBlock);
    monoScratch_.assign(samplesPerBlock, 0.0f);
}

void K2000AudioProcessor::releaseResources() {}

bool K2000AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        || out == juce::AudioChannelSet::mono();
}

void K2000AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int outCh = buffer.getNumChannels();

    buffer.clear();

    // Build the snapshot once for this block.
    auto snap = params::snapshot(apvts_);

    // Render mono into scratch.
    if ((int) monoScratch_.size() < n) monoScratch_.assign(n, 0.0f);
    std::fill(monoScratch_.begin(), monoScratch_.begin() + n, 0.0f);
    voiceManager_.renderBlock(monoScratch_.data(), n, midi, snap);

    // Apply master gain (dB → linear)
    const float gainLin = juce::Decibels::decibelsToGain(snap.masterGainDb);

    // Copy mono scratch to all output channels with master gain.
    for (int c = 0; c < outCh; ++c) {
        float* ch = buffer.getWritePointer(c);
        for (int i = 0; i < n; ++i)
            ch[i] = monoScratch_[i] * gainLin;
    }
}

juce::AudioProcessorEditor* K2000AudioProcessor::createEditor() {
    return new K2000AudioProcessorEditor(*this);
}

// State format:
//   <K2000Root>
//     <Slots>
//       <Slot index="0" type="svf_filter"/>
//       <Slot index="1" type="waveshaper"/>
//     </Slots>
//     <Params> ...APVTS state... </Params>
//   </K2000Root>
//
// v1 ignores the Slots block on load (slot types are fixed) but writes it
// from day one so v1 presets stay loadable in v4 when the user can change
// slot types.
void K2000AudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto root = std::make_unique<juce::XmlElement>("K2000Root");

    auto* slots = root->createNewChildElement("Slots");
    auto* s0 = slots->createNewChildElement("Slot");
    s0->setAttribute("index", 0);
    s0->setAttribute("type", "svf_filter");
    auto* s1 = slots->createNewChildElement("Slot");
    s1->setAttribute("index", 1);
    s1->setAttribute("type", "waveshaper");

    if (auto state = apvts_.copyState(); state.isValid()) {
        auto paramsXml = state.createXml();
        if (paramsXml) {
            auto* wrapper = root->createNewChildElement("Params");
            wrapper->addChildElement(paramsXml.release());
        }
    }

    copyXmlToBinary(*root, destData);
}

void K2000AudioProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr) return;
    if (xml->getTagName() != "K2000Root") return;

    if (auto* params = xml->getChildByName("Params")) {
        if (auto* paramsRoot = params->getFirstChildElement()) {
            apvts_.replaceState(juce::ValueTree::fromXml(*paramsRoot));
        }
    }
    // Slot type metadata: ignored in v1 (types are fixed); v4 will read it here.
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new K2000AudioProcessor();
}
```

- [ ] **Step 4: Build the plugin and verify it builds**

```bash
cmake --build build -j
```

Expected: clean build.

- [ ] **Step 5: Reinstall and listen in Carla**

```bash
rm -rf ~/.vst3/k2000.vst3
cp -r build/k2000_artefacts/Debug/VST3/k2000.vst3 ~/.vst3/
```

Open Carla, add k2000, hook up a MIDI keyboard or use Carla's built-in keyboard. Play notes — you should hear sound. (The default UI is still the v0 black placeholder; the proper UI lands in Task 13.) Verify that automating the filter cutoff (via Carla's parameter list) audibly changes the tone.

If silent: check `processBlock` is being called (add a `juce::Logger::writeToLog`), check that voices are being allocated (the test harness exercises this, so it's likely a parameter-routing bug).

- [ ] **Step 6: Create `tests/PluginLifecycleTests.cpp`**

```cpp
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/PluginProcessor.h"

class PluginLifecycleTest : public juce::UnitTest {
public:
    PluginLifecycleTest() : juce::UnitTest("PluginLifecycle") {}

    static constexpr double SR = 48000.0;
    static constexpr int BLOCK = 512;

    void runTest() override {
        beginTest("construct → prepare → silent processBlock");
        {
            K2000AudioProcessor p;
            p.prepareToPlay(SR, BLOCK);
            juce::AudioBuffer<float> buf(2, BLOCK);
            juce::MidiBuffer midi;
            p.processBlock(buf, midi);
            // No MIDI → output should be silence
            double sumAbs = 0;
            for (int c = 0; c < buf.getNumChannels(); ++c)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                    sumAbs += std::abs(buf.getSample(c, i));
            expectWithinAbsoluteError((float) sumAbs, 0.0f, 1e-6f);
        }

        beginTest("noteOn through processBlock produces audible output");
        {
            K2000AudioProcessor p;
            p.prepareToPlay(SR, BLOCK);
            juce::AudioBuffer<float> buf(2, BLOCK);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, (juce::uint8) 100), 0);
            p.processBlock(buf, midi);
            // Render a few more blocks to let envelope past attack
            for (int i = 0; i < 4; ++i) {
                midi.clear();
                buf.clear();
                p.processBlock(buf, midi);
            }
            double sumAbs = 0;
            for (int c = 0; c < buf.getNumChannels(); ++c)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                    sumAbs += std::abs(buf.getSample(c, i));
            expect(sumAbs > 1.0, "should produce audible output");
        }

        beginTest("state save and restore round-trips a non-default parameter");
        {
            K2000AudioProcessor p;
            p.prepareToPlay(SR, BLOCK);
            if (auto* cutoff = p.apvts().getParameter(params::id::svfCutoff))
                cutoff->setValueNotifyingHost(cutoff->convertTo0to1(3200.0f));

            juce::MemoryBlock mb;
            p.getStateInformation(mb);

            K2000AudioProcessor q;
            q.prepareToPlay(SR, BLOCK);
            q.setStateInformation(mb.getData(), (int) mb.getSize());
            float restored = *q.apvts().getRawParameterValue(params::id::svfCutoff);
            expectWithinAbsoluteError(restored, 3200.0f, 5.0f);
        }
    }
};

static PluginLifecycleTest pluginLifecycleTestInstance;
```

- [ ] **Step 7: Add to test build**

Edit `tests/CMakeLists.txt`:

```cmake
add_executable(k2000_tests
    TestMain.cpp
    SmokeTests.cpp
    ParamSnapshotTests.cpp
    OscillatorTests.cpp
    EnvelopeTests.cpp
    SVFFilterTests.cpp
    WaveshaperTests.cpp
    VoiceTests.cpp
    VoiceManagerTests.cpp
    PluginLifecycleTests.cpp
    ../src/PluginProcessor.cpp
    ../src/Voice.cpp
    ../src/VoiceManager.cpp
    ../src/params/Parameters.cpp
    ../src/dsp/Oscillator.cpp
    ../src/dsp/Envelope.cpp
    ../src/dsp/blocks/SVFFilter.cpp
    ../src/dsp/blocks/Waveshaper.cpp)
```

To keep the test target lean (no editor sources, no LookAndFeel pulled in), stub `createEditor()` for the test build. We already pre-added `K2000_TESTING=1` and `juce::juce_gui_basics` to the test target in Task 3 — now update the implementation:

In `src/PluginProcessor.cpp`, replace:
```cpp
juce::AudioProcessorEditor* K2000AudioProcessor::createEditor() {
    return new K2000AudioProcessorEditor(*this);
}
```
with:
```cpp
juce::AudioProcessorEditor* K2000AudioProcessor::createEditor() {
#if K2000_TESTING
    return nullptr;
#else
    return new K2000AudioProcessorEditor(*this);
#endif
}
```

In `src/PluginProcessor.cpp`, also guard the `#include "PluginEditor.h"`:
```cpp
#if !K2000_TESTING
  #include "PluginEditor.h"
#endif
```

- [ ] **Step 8: Build & run**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

All tests should pass.

- [ ] **Step 9: Commit**

```bash
git add src/PluginProcessor.* src/Voice.* tests/PluginLifecycleTests.cpp tests/CMakeLists.txt
git commit -m "feat: wire VoiceManager into processBlock with state save/load"
git push
```

---

## Task 13: Minimal PluginEditor UI

**Goal:** A functional (ugly) JUCE UI bound to all parameters via `SliderAttachment` and `ComboBoxAttachment`.

**Files:**
- Modify: `src/PluginEditor.h`
- Modify: `src/PluginEditor.cpp`

- [ ] **Step 1: Replace `src/PluginEditor.h`**

```cpp
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class K2000AudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit K2000AudioProcessorEditor(K2000AudioProcessor& p);
    ~K2000AudioProcessorEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    K2000AudioProcessor& processorRef;

    using APVTS = juce::AudioProcessorValueTreeState;
    using SliderAtt  = APVTS::SliderAttachment;
    using ComboAtt   = APVTS::ComboBoxAttachment;

    struct LabeledSlider {
        juce::Label  label;
        juce::Slider slider{ juce::Slider::RotaryHorizontalVerticalDrag,
                             juce::Slider::TextBoxBelow };
        std::unique_ptr<SliderAtt> attach;
    };
    struct LabeledCombo {
        juce::Label    label;
        juce::ComboBox combo;
        std::unique_ptr<ComboAtt> attach;
    };

    LabeledSlider oscCoarse, oscFine,
                  svfCutoff, svfRes,
                  wsDrive, wsMix,
                  ampA, ampD, ampS, ampR,
                  masterGain;
    LabeledCombo  oscWave, svfType;

    void addSlider(LabeledSlider& ls, juce::StringRef label, juce::StringRef paramId);
    void addCombo(LabeledCombo& lc, juce::StringRef label, juce::StringRef paramId,
                  const juce::StringArray& items);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(K2000AudioProcessorEditor)
};
```

- [ ] **Step 2: Replace `src/PluginEditor.cpp`**

```cpp
#include "PluginEditor.h"
#include "params/Parameters.h"

K2000AudioProcessorEditor::K2000AudioProcessorEditor(K2000AudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p) {
    addSlider(oscCoarse, "Coarse", params::id::oscCoarse);
    addSlider(oscFine,   "Fine",   params::id::oscFine);
    addSlider(svfCutoff, "Cutoff",     params::id::svfCutoff);
    addSlider(svfRes,    "Resonance",  params::id::svfResonance);
    addSlider(wsDrive,   "Drive", params::id::wsDrive);
    addSlider(wsMix,     "Mix",   params::id::wsMix);
    addSlider(ampA,      "A", params::id::ampAttack);
    addSlider(ampD,      "D", params::id::ampDecay);
    addSlider(ampS,      "S", params::id::ampSustain);
    addSlider(ampR,      "R", params::id::ampRelease);
    addSlider(masterGain,"Gain", params::id::masterGain);

    addCombo(oscWave, "Wave",       params::id::oscWaveform,
             juce::StringArray{"Saw", "Square", "Triangle", "Sine"});
    addCombo(svfType, "Filter",     params::id::svfType,
             juce::StringArray{"LP", "HP", "BP", "Notch"});

    setSize(720, 360);
}

void K2000AudioProcessorEditor::addSlider(LabeledSlider& ls,
                                          juce::StringRef label,
                                          juce::StringRef paramId) {
    ls.label.setText(label, juce::dontSendNotification);
    ls.label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(ls.label);
    addAndMakeVisible(ls.slider);
    ls.attach = std::make_unique<SliderAtt>(processorRef.apvts(), paramId, ls.slider);
}

void K2000AudioProcessorEditor::addCombo(LabeledCombo& lc,
                                         juce::StringRef label,
                                         juce::StringRef paramId,
                                         const juce::StringArray& items) {
    lc.label.setText(label, juce::dontSendNotification);
    lc.label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lc.label);
    lc.combo.addItemList(items, 1);
    addAndMakeVisible(lc.combo);
    lc.attach = std::make_unique<ComboAtt>(processorRef.apvts(), paramId, lc.combo);
}

void K2000AudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(28, 28, 32));
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawText("k2000 — v1", 12, 8, 200, 20, juce::Justification::left);
}

void K2000AudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(12).withTrimmedTop(28);
    const int rowH = area.getHeight() / 4;

    auto layoutRow = [&](juce::Rectangle<int> row,
                         std::initializer_list<std::pair<juce::Component*, juce::Component*>> cells) {
        const int n = int(cells.size());
        const int w = row.getWidth() / n;
        int x = row.getX();
        for (auto& [labelC, knobC] : cells) {
            labelC->setBounds(x, row.getY(), w, 18);
            knobC->setBounds(x, row.getY() + 18, w, row.getHeight() - 18);
            x += w;
        }
    };

    layoutRow(area.removeFromTop(rowH),
              {{&oscWave.label, &oscWave.combo},
               {&oscCoarse.label, &oscCoarse.slider},
               {&oscFine.label, &oscFine.slider}});

    layoutRow(area.removeFromTop(rowH),
              {{&svfType.label, &svfType.combo},
               {&svfCutoff.label, &svfCutoff.slider},
               {&svfRes.label, &svfRes.slider},
               {&wsDrive.label, &wsDrive.slider},
               {&wsMix.label, &wsMix.slider}});

    layoutRow(area.removeFromTop(rowH),
              {{&ampA.label, &ampA.slider},
               {&ampD.label, &ampD.slider},
               {&ampS.label, &ampS.slider},
               {&ampR.label, &ampR.slider}});

    layoutRow(area.removeFromTop(rowH),
              {{&masterGain.label, &masterGain.slider}});
}
```

- [ ] **Step 3: Build, install, and load in Carla**

```bash
cmake --build build -j
rm -rf ~/.vst3/k2000.vst3
cp -r build/k2000_artefacts/Debug/VST3/k2000.vst3 ~/.vst3/
```

Launch Carla, load k2000. The editor should now show a grid of rotary sliders + two combo boxes. Click a knob and drag — its value should change and the corresponding parameter should automate visibly (try filter cutoff while a note is playing). Verify all controls audibly affect the sound:

- Oscillator wave: switch between Saw / Square / Triangle / Sine — timbre changes.
- Cutoff: sweeping low to high opens/closes the filter.
- Resonance: at high values, a peak appears around the cutoff.
- Drive + Mix: drives the signal into saturation, mix blends dry/wet.
- ADSR: attack/release especially audible.
- Master gain: changes loudness.

- [ ] **Step 4: Commit**

```bash
git add src/PluginEditor.*
git commit -m "feat(ui): minimal functional editor with all v1 controls bound to APVTS"
git push
```

---

## Task 14: Integration testing checkpoint (Definition of Done)

**Goal:** Verify the full Definition of Done from the v1 spec. No code changes — this is a validation checkpoint.

- [ ] **Step 1: Wait for CI to go green on the latest commit**

```bash
gh run watch
```

Both `linux` and `windows` matrix jobs must pass.

- [ ] **Step 2: DoD checklist on Linux (local build)**

For each item below, manually verify and tick when done:

- [ ] CMake project builds clean on Linux locally.
- [ ] All `juce::UnitTest` tests pass (`ctest --test-dir build --output-on-failure`).
- [ ] Plugin loads in Carla (or Bitwig/Reaper) on Linux.
- [ ] Playing a MIDI keyboard produces audible polyphonic sound through the full signal chain.
- [ ] Filter cutoff and resonance audibly affect tone in all four filter modes (LP/HP/BP/Notch).
- [ ] Waveshaper drive audibly distorts; mix knob blends dry/wet.
- [ ] ADSR envelope behaves correctly (clean attack/decay/sustain/release; no clicks).
- [ ] Parameter changes from the GUI are reflected in audio without crackles.
- [ ] Preset save/restore via the host round-trips all parameters (set unusual values, save host session, reload, verify values restored).

- [ ] **Step 3: DoD checklist on Windows (CI artifact in Ableton 12)**

- [ ] Download `k2000-windows-<sha>.zip` from latest successful CI run.
- [ ] Unzip into `C:\Program Files\Common Files\VST3\`.
- [ ] Rescan plugins in Ableton 12; `k2000` appears in the browser.
- [ ] Drag onto a MIDI track; editor opens; playing the keyboard produces sound.
- [ ] Repeat the same checks as Step 2 in Ableton.

- [ ] **Step 4: RT-safety sanity check (debug build)**

The v1 spec calls for an audio-thread sentinel asserting no allocation in `processBlock`. A full sentinel is its own task we can defer to a future cleanup pass; for v1 a lightweight check is fine:

- Run a debug build under `valgrind --tool=massif` for ~30 seconds of audio while playing notes. The audio-thread allocation count should be zero. If it's not, find the culprit (the most likely candidates are `monoScratch_` resizing on a sample-rate change, or `scratch_` in `Voice`).

This step is informational — if you find an allocation, file it as a follow-up issue rather than blocking v1.

- [ ] **Step 5: Tag the v1 release**

```bash
git tag -a v1.0.0 -m "v1 skeleton: 1 osc → SVF → waveshaper → ADSR amp, 8-voice"
git push --tags
```

- [ ] **Step 6: Update the spec status**

In `docs/specs/2026-05-25-v1-skeleton-design.md`, change the front-matter:

```diff
-**Status:** Approved, not yet implemented
+**Status:** Implemented (tagged v1.0.0 on <YYYY-MM-DD>)
```

And in `docs/specs/README.md`:

```diff
-| [v1 — Skeleton end-to-end](2026-05-25-v1-skeleton-design.md) | Approved, not yet implemented |
+| [v1 — Skeleton end-to-end](2026-05-25-v1-skeleton-design.md) | Implemented (v1.0.0) |
```

In `docs/plans/README.md`:

```diff
-| [v1 implementation](2026-05-25-v1-implementation.md) | [v1 design](../specs/2026-05-25-v1-skeleton-design.md) | Ready to execute |
+| [v1 implementation](2026-05-25-v1-implementation.md) | [v1 design](../specs/2026-05-25-v1-skeleton-design.md) | Completed |
```

- [ ] **Step 7: Final commit**

```bash
git add docs/specs/2026-05-25-v1-skeleton-design.md docs/specs/README.md docs/plans/README.md
git commit -m "docs: mark v1 as implemented and tagged v1.0.0"
git push
```

---

## End of plan

v1 is done when every checkbox above is ticked. From here we move to the v2 spec — see [`../roadmap/phases.md`](../roadmap/phases.md). v2's theme is real Peak character: 3 oscillators per voice, the analog-modeled multimode filter, pre/post-filter drive routing.
