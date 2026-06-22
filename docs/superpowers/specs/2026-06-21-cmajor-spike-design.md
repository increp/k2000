# Cmajor Spike — Design (pre-v6 decision gate)

**Version:** 5.02 (artifact; distinct from plugin SemVer)
**Date:** 2026-06-21
**Status:** Approved (brainstorm) — pending spec review
**Roadmap item:** `cmajor-spike` (decision gate before v6). Produces **ADR-0012**.

---

## 1. Purpose & premise

Evaluate **Cmajor** (Sound Stacks' C-family DSP language, by JUCE's creator) as a DSP layer for Bernie — **not** as a replacement for JUCE. The question is **coexistence**: JUCE keeps owning the host/UI/parameters/MIDI/voice-pool, and Cmajor potentially supplies the **per-voice DSP** (filters now; the v6 graph later) where it is a better fit. The deliverable is **evidence for a go / no-go / hybrid decision**, captured in ADR-0012 — it must resolve **before v6 is designed**, because authoring the v6 graph engine twice (C++ then Cmajor) is the cost we are trying to avoid. See [[feedback-cmajor-coexist-juce]].

This is a **spike**: a small, bounded, throwaway-friendly experiment that answers specific questions, not a feature. Its code is compiled into the **test target only** and never touches the shipping VST3/Standalone binary.

---

## 2. Decisions (resolved in brainstorm, 2026-06-21)

- **Licensing path → AOT generated-C++ only.** Cmajor is dual GPLv3/commercial. **Generated C++ from our own Cmajor source is ours**, free to ship in a closed-source commercial plugin. Embedding the Cmajor **engine/JIT/runtime** (the `cmaj::Patch` / `GeneratedCppEngine` host machinery) would require GPLv3-ing Bernie or buying a commercial license — both rejected (Bernie is commercial closed-source per roadmap v13/v25). **Therefore the spike pilots only the path of: write DSP in Cmajor → `cmaj generate --target=cpp` → embed the dependency-free generated class. The Cmajor toolchain is a dev-time codegen tool, never shipped, never a runtime dependency.**
- **Codegen workflow → commit the generated C++.** Run `cmaj generate --target=cpp` on the dev machine; commit the resulting `.h/.cpp` **and** the `.cmajor` source. CI and every build just compile the committed C++ — **no Cmajor toolchain in CI**. Regenerate only when the `.cmajor` changes.
- **Integration depth → thin `FilterModel` wrapper, benched.** Wrap the generated SVF class in `CmajorSvfFilter : FilterModel`, exercised through the existing test harness and the spine `FilterModel` path. **Not** registered in the live `FilterModelLibrary`; **not** compiled into the plugin binary. Proves it slots into Bernie's per-voice architecture without shipping spike code.
- **Pilot surface → the linear TPT SVF only.** Port the `NlSvfCell` **linear** core (cutoff/resonance, LP/BP/HP taps). The nonlinear resonance-saturator is **out of scope** for the pilot — equivalence + perf on the linear core is sufficient go/no-go signal.

---

## 3. Architecture & files

Everything spike-related is isolated; the shipping plugin is untouched.

```
src/dsp/spine/cmajor/                 (compiled into the TEST target only)
  SvfLinear.cmajor                    committed Cmajor source (the pilot DSP)
  generated/SvfLinear.h               committed `cmaj generate --target=cpp` output
  generated/SvfLinear.cpp             (regenerated only when SvfLinear.cmajor changes)
  CmajorSvfFilter.h/.cpp              FilterModel wrapper holding one generated instance per voice
tests/
  CmajorToolchainTests.cpp           trivial gain-patch round-trip (toolchain smoke)
  CmajorSvfEquivalenceTests.cpp      generated SVF vs NlSvfCell via testdsp
  CmajorSvfPerfTests.cpp             256-instance perf bench (generated vs NlSvfCell)
docs/decisions/0012-*.md             ADR-0012 (the spike's verdict)
```

**Wrapper interface (`CmajorSvfFilter : FilterModel`):** mirrors `HuggettFilter`'s shape so the comparison is apples-to-apples —
- `void prepare(double sampleRate)` — set sample rate on the generated instance(s).
- `State* makeState() const` — per-voice state owning one generated SVF instance (heap-allocated like the existing models; the spike does not need the v5.1 in-place migration).
- `void setCommon(float cutoffHz, float resonance, float drive)` — maps cutoff/res to the generated patch's parameter endpoints (drive ignored — linear pilot).
- `void processStereo(State&, float* l, float* r, int n)` — render a block through the generated class's advance/render API; one tap (LP/BP/HP) selected via a wrapper setter.

**Generated-class API (from `cmaj generate --target=cpp`):** the dependency-free class exposes static constants + `initialise(sampleRate, ...)`, `setValue(endpointHandle, value)` for parameters, and an `advance()`/render loop over input/output endpoints. The wrapper adapts Bernie's `(float* l, float* r, int n)` block call onto that API. **Exact endpoint names/handles are discovered during Task 2** (port) and pinned in the wrapper then — they are an output of writing `SvfLinear.cmajor`, not a guess.

---

## 4. The funnel (build sequence + gates)

Ordered so each step can **kill the spike cheaply** before the next:

1. **Toolchain + round-trip.** Install the `cmaj` CLI on the dev machine (Linux). Write a trivial gain `.cmajor`, `cmaj generate --target=cpp`, compile the generated class into a test, feed a buffer, assert the gain is applied. **Gate:** the generate→compile→run loop works end-to-end. (License already confirmed: §2.)
2. **Port the linear SVF.** Write `SvfLinear.cmajor` (TPT/ZDF SVF: cutoff/resonance inputs, LP/BP/HP outputs), generate + commit the C++. **Gate:** it compiles and produces non-trivial filtered output.
3. **Equivalence.** Bench the generated SVF against `NlSvfCell` (resonance-sat off) across a cutoff×resonance grid using the `testdsp` magnitude-response helpers. **Gate:** magnitude responses match within **~0.5 dB** (assert; CI-fails on regression).
4. **`FilterModel` wrapper + integration proof.** Implement `CmajorSvfFilter : FilterModel`; drive it through the spine `FilterModel` path with params from the snapshot pattern; confirm **RT-safety** (no audio-thread allocation in `processStereo`). **Gate:** audio flows through the wrapper with correct, parameter-driven behaviour.
5. **256-voice perf (the crux).** Benchmark 256 instances of the generated class vs 256 `NlSvfCell`s processing equal work; report the **CPU ratio** and per-instance memory. **Gate:** a measured ratio vs the C++ baseline — *no hard pass/fail*; this is the primary input the ADR judges. (Cmajor's idiomatic in-patch polyphony via `std::voices::VoiceAllocator` is noted as the alternative model but is NOT piloted — Bernie keeps its C++ voice pool in the coexistence design.)
6. **CI compiles the committed generated C++.** Confirm the checked-in generated `.h/.cpp` + wrapper + tests build green in the Windows GitHub Actions CI. **Gate:** CI green. (This is ordinary C++ — no Cmajor toolchain in CI, by the §2 codegen decision.)
7. **ADR-0012 + recommendation.** Synthesize equivalence, the 256-voice perf number, integration friction, the (already-clean) licensing position, and dev experience into ADR-0012. **Outcomes are not binary:** a **hybrid** (Cmajor for some/all DSP, JUCE for host/UI/params) is a valid recommendation. If adopting, define the JUCE↔Cmajor boundary, the `cmajor-migration` slot/approach, and whether v6's graph is authored in Cmajor.

Build with bounded parallelism (`-j4`); never bare `-j`.

---

## 5. Testing

All gates are JUCE `UnitTest`s in the existing `tests/` suite (`./build/tests/k2000_tests`), reusing `tests/testdsp` for magnitude-response/NSR. Equivalence + RT-safety are hard asserts (CI-failing). The perf bench prints its ratio and asserts only finiteness/sanity — the number is a decision input, not a pass/fail gate. The toolchain round-trip (Task 1) depends on `cmaj` being installed on the dev machine; if it is absent the test is skipped with a clear message (it is a dev-time check, not a CI gate, since CI compiles committed generated C++).

---

## 6. Out of scope (YAGNI for the spike)

- Shipping `CmajorSvfFilter` in the plugin / registering it in `FilterModelLibrary`.
- The nonlinear resonance-saturator, drive stages, or the dual-section separation in Cmajor.
- Porting any second filter model, the v6 graph, or moving Bernie's voice pool into a Cmajor patch.
- The Cmajor JIT/runtime engine, `cmaj::Patch`, hot-reload, or any runtime Cmajor dependency.
- Build-time codegen / Cmajor toolchain in CI.
- The actual migration (that is `cmajor-migration`, whose shape ADR-0012 decides).

---

## 7. Success criteria

The spike succeeds when ADR-0012 can be written with: a measured equivalence result, a measured 256-voice CPU/memory ratio vs the C++ baseline, a concrete read on integration/build friction, and a clear **go / no-go / hybrid** recommendation with the JUCE↔Cmajor boundary it implies. A *negative* result (Cmajor not worth adopting, or only for a narrow slice) is a fully successful spike outcome.
