# ADR 0012 — Cmajor coexistence evaluation (pre-v6 gate)

**Status:** Proposed, 2026-06-21 — pending user sign-off. **Version:** 5.02. Decides the `cmajor-spike` gate; gates how v6's DSP graph is authored.

## Context

Before [v6](../../tools/roadmap-dashboard/roadmap.json) (the dynamic VAST graph) is designed, we must answer one question: **is the v6 per-voice DSP graph authored in C++ or in [Cmajor](https://cmajor.dev)?** Authoring a graph engine twice — once in C++, then again in Cmajor after the fact — is the expensive mistake this gate exists to prevent. The framing is **coexistence, not replacement** (see [[feedback-cmajor-coexist-juce]]): JUCE keeps owning the host, UI, parameters, MIDI, and the voice pool; the only question is whether Cmajor supplies the **per-voice DSP** where it is a better fit. A *hybrid* outcome is first-class, not a consolation prize.

Two constraints were settled in the spike brainstorm ([spec §2](../superpowers/specs/2026-06-21-cmajor-spike-design.md)) and hold here:

- **AOT generated-C++ only.** Cmajor is dual GPLv3/commercial. **C++ generated from our own `.cmajor` source is ours** — free to ship in a closed-source commercial plugin. Embedding the Cmajor **engine/JIT/runtime** (`cmaj::Patch`, `GeneratedCppEngine`) would force GPLv3 on Bernie or a commercial license — both rejected. So the only path piloted (and the only one we would adopt) is: write DSP in Cmajor → `cmaj generate --target=cpp` → embed the dependency-free generated class. The toolchain is a **dev-time codegen tool**, never shipped, never a runtime dependency.
- **Commit the generated C++.** Codegen runs on the dev machine; the `.cmajor` source **and** the generated `.h` are committed. CI and every build just compile ordinary C++ — **no Cmajor toolchain in CI**.

The spike piloted the **linear TPT SVF only** (cutoff/resonance, LP/BP/HP), wrapped in a test-target-only `CmajorSvfFilter : FilterModel` and benched against the shipping `NlSvfCell`. None of the spike code is in the plugin binary or registered in `FilterModelLibrary`.

## Evidence

All figures from the `feat/cmajor-spike` branch; tests run via `./build/tests/k2000_tests` (full suite **149 tests, 0 failed**).

- **Numeric equivalence (clean PASS).** The Cmajor SVF matches `NlSvfCell`'s linear core (resonance-saturator off) **within 0.5 dB across all 45 grid points** (3 cutoffs × 3 taps × 5 frequencies) — `CmajorSvfEquivalenceTests`. No tolerance had to be loosened; the ported math is exact. The same is visible in the perf bench, where both paths produced bit-identical output sinks (`-690.415`).

- **256-voice CPU + memory (the crux).** `CmajorSvfPerfTests`, 256 voices × 200 blocks × 128 samples @ 48 kHz:

  | Path | Time | Per-instance size |
  |---|---|---|
  | Cmajor adapter (AOT C++) | **25.36 ms** | `sizeof = 8` (a `unique_ptr` handle; generated state on the heap) |
  | `NlSvfCell` (hand-written C++) | **24.65 ms** | `sizeof = 72` (inline state) |
  | **Ratio** | **1.03× (≈3% slower)** | small handle + 1 heap alloc/voice vs 72 B inline |

  CPU is at **parity** — the AOT-generated C++ is within noise of hand-written C++. The memory tradeoff is the only real per-voice cost: a heap allocation per generated instance (mitigable; not piloted).

- **Integration friction (clean).** `CmajorSvfFilter : FilterModel` (`CmajorFilterModelTests`) slots into Bernie's per-voice spine with no interface friction — stereo via two adapters per `VoiceState`, channels bit-identical for identical input, LP attenuates highs, large blocks stay finite. `processStereo` is **allocation-free** (only `setParams`/`process` on pre-made adapters; `new` confined to `makeState`). The unknown generated-class API is isolated behind a hand-written `SvfLinearAdapter` (stable `prepare/reset/setParams/process`), so downstream code never touches generated specifics.

- **Generated render model.** The `cmaj generate --target=cpp` output is a self-contained struct with a **block-oriented** render loop (`setInputFrames` → `addEvent_*` params → `advance(n)` → `copyOutputFrames`), capped at `maxFramesPerBlock = 512` (the I/O buffers are `Array<float,512>`). The adapter chunks larger blocks transparently. This is a clean, conventional API to wrap.

- **Dev-experience friction (the one wart).** The prebuilt Linux `cmaj` 1.0.3066 is built against Ubuntu-22.04 libraries and **does not run natively on this 24.04 box**; codegen runs inside an `ubuntu:22.04` Docker container via [`tools/cmajor/cmaj-codegen.sh`](../../tools/cmajor/cmaj-codegen.sh) (see [[reference-cmaj-toolchain]]). Because codegen is one-shot and its output is committed, this is a **dev-time-only** dependency — it never reaches CI or the shipping build. Writing the SVF in Cmajor was pleasant (concise, readable, close to the DSP math); the only cost was standing up the container.

## Decision

**GO — adopt Cmajor as a coexistence DSP layer (hybrid), pending the nonlinear follow-up below.**

- **Boundary:** **JUCE owns** the host, UI, parameters, MIDI, preset/state, and the voice pool. **Cmajor owns** per-voice DSP block math (filters, shapers, oscillators, the v6 graph nodes) via AOT generated-C++ wrapped behind thin C++ adapters that present Bernie's existing interfaces (`FilterModel`-style). Bernie keeps its **C++ voice pool** — we do *not* move polyphony into a Cmajor patch (`std::voices::VoiceAllocator` is noted but not adopted).
- **v6 graph:** author the v6 per-voice DSP graph **in Cmajor**, generated to committed C++. This is the decision the gate exists for — it avoids the double-build. (Conditional on the nonlinear follow-up landing cleanly; see Consequences.)
- **Workflow:** `.cmajor` source + generated `.h` are committed together; regenerate only when the source changes; CI compiles plain C++ with no Cmajor dependency.
- **`cmajor-migration`:** reframed from "conditional/tentative" to a **planned hybrid adoption** whose first concrete step is porting a real shipping DSP block (not throwaway spike code) behind its existing interface.

Rationale: equivalence is exact, CPU is at parity, integration is frictionless, and the licensing position is clean. The only debits — a per-voice heap allocation and dev-time Docker codegen — are both mitigable and neither touches the shipping binary or CI.

## Consequences

**Commits us to:** authoring v6's graph DSP in Cmajor; maintaining the codegen-commit workflow ([`cmaj-codegen.sh`](../../tools/cmajor/cmaj-codegen.sh) + committed generated C++); a thin-adapter discipline so generated specifics never leak past one wrapper per block.

**Avoids:** building the v6 graph engine twice; any GPL/commercial-license exposure (engine never embedded); any Cmajor dependency in CI or the shipping VST3/Standalone.

**Costs / follow-ups (gating full ratification):**
- **Nonlinear follow-up spike (required before v6 commits).** The pilot was the *linear* core only. Before authoring v6 in Cmajor, port one **nonlinear** stage (the Huggett resonance-saturator / tanh drive) and re-run equivalence + 256-voice perf — nonlinear math + oversampling is where a perf or fidelity surprise would surface.
- **Per-voice allocation.** Replace the `unique_ptr`-to-heap adapter state with in-place storage before any shipping use (dovetails with the v5.1 heap→in-place `State` migration, register Q17/Q18).
- **Windows CI confirmation.** The committed generated C++ + adapters live in the test target, which Windows CI builds — confirm green on the next CI run (ordinary C++, expected trivial).
- **Toolchain ergonomics.** Optionally build a cached codegen image (the script apt-installs webkit each run) and document the codegen step for contributors.

**Supersedes:** the open question in [[feedback-cmajor-coexist-juce]] ("where does the JUCE↔Cmajor boundary sit") — answered above: Cmajor below the per-voice DSP-block line, JUCE everywhere else.
