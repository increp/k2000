# Static Analysis (SAST)

**Version:** 5.12 · **Date:** 2026-07-02 · Part of the [codebase health audit](README.md).

## Method

All 28 hand-written translation units (from `build/compile_commands.json`,
excluding `cmajor/generated`) recompiled with
`gcc-13 -fanalyzer -Wextra -Wshadow -c -o /dev/null`. 19 TUs produced
findings; classified below.

## Findings

### P2 — version-surface drift (process failure, third occurrence)

`CMakeLists.txt` declares `project(k2000 VERSION 5.4.0)` while current docs
(SP-A spec, filter-validation manual) describe the plugin as 5.9.0. The panel
label went stale the same way at v2 and v3 ship. Reconcile the truth, then
implement the standing note: derive the panel label from
`JucePlugin_VersionString` so CMake is the single source. This belongs to the
anti-drift harness as a checkable invariant (grep CMake VERSION ↔ docs claim).

### P2 — deprecated `juce::Font(float, int)` (4 sites)

| Site | |
|---|---|
| `src/PluginEditor.cpp:28` | title font |
| `src/gui/SummitLookAndFeel.cpp:61` | combo font |
| `src/gui/LabeledKnob.cpp:9` | caption font |
| `src/gui/Section.cpp:32` | header font |

JUCE 8 wants the `FontOptions` constructor. Mechanical migration; do it before
any JUCE bump (the deprecation becomes a removal).

### P3 — `-Wsign-conversion` inventory (~92 warnings, known/deferred)

Concentrated in `src/Layer.h` (array indexing with `int`), `src/Voice.cpp`,
`src/VoiceManager.cpp`, `src/PluginProcessor.cpp:19-20`,
`src/dsp/spine/SpineFilterSlot.cpp:93-94`, plus one per Cmajor generated
header (upstream codegen; not ours to fix — regenerate or suppress at the
include site). All are non-negative `int` → `size_t` index conversions —
benign today, but they are exactly the noise floor under which a real
truncation bug would hide. The standing deferred cleanup stands; suggest
doing it file-by-file when each file is next touched.

### Analyzer findings triaged as false positives (verified by reading)

- `CWE-457 use-of-uninitialized-value` / `CWE-476 null-argument` in
  `FilterModelLibrary.cpp:23-42`, `util/Utf8.h:17`, `Waveshaper.cpp:8`,
  `SummitLookAndFeel.cpp:61` — all land inside `juce::String` /
  `CharPointer_UTF8` internals on string-literal paths; GCC's analyzer cannot
  see JUCE's invariants. The call sites pass compile-time string literals
  (verified). No action; a `clang-tidy` pass would confirm independently.

## Tool gap

No `clang-tidy` / `cppcheck` / `valgrind` / `pluginval` on this machine and no
passwordless sudo. For the deeper pass:

```bash
sudo apt install cppcheck clang-tidy valgrind
# pluginval: download release binary from github.com/Tracktion/pluginval
```

`pluginval --strictness-level 10` against the built VST3 is the single
highest-value addition (exercises host lifecycle edges the unit suite
doesn't: bus renegotiation, state round-trips under threads, editor
open/close cycles).
