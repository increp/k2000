# ADR 0012 — Cmajor coexistence evaluation (pre-v6 gate)

**Status:** Accepted, 2026-06-21 (ratified after Spike I + II). **Version:** 5.03. Decides the `cmajor-spike` gate; gates how v6's DSP graph is authored.

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

## Evidence — Spike II addendum (Nonlinear + Data-Boundary), 2026-06-21

Spike I piloted only the *linear* SVF. [Spike II](../superpowers/specs/2026-06-21-cmajor-nonlinear-spike-design.md) ([plan](../superpowers/plans/2026-06-21-cmajor-nonlinear-spike.md)) closes the two questions that gated ratification: does *nonlinear* DSP port faithfully + perform, and can array/sample data cross the boundary. All test-target-only on `feat/cmajor-spike`; full suite 155 tests, 0 failed.

- **Nonlinear equivalence — bit-exact.** The in-loop resonance saturator (`NlSvf` vs `NlSvfCell` resSat-on, 81 configs over cutoff×res×level×tap) and the feedforward drive shaper (`AsymDrive` vs `AsymSaturator`, 4 drive levels) both matched **per-sample with worst error 0.0** — no harmonic fallback needed. Notably Cmajor's `tanh`/`pow` intrinsics resolve to the same libm as `std::`, so even the transcendental-heavy path is bit-identical. The fused chain (below) matched the C++ `NlSvfCell→AsymSaturator` chain to 4.4e-6 (rounding from combined expressions; effectively exact).

- **Data boundary — solved, with a constraint.** `external float[]` **fails to resolve at codegen** unless bound in the manifest — i.e. it is *baked at generate time*, not runtime-settable, in the `--target=cpp` path (fine for factory wavetables, useless for user-loaded data). A fixed-size **`input value` array endpoint is runtime-settable** via the generated `setValue(handle, ptr, frames)`: a `WtOsc` wavetable oscillator played back a table pushed from C++ correctly (energy concentrated at the played pitch). **Reusable answer for wavetables/granular:** factory tables → bake via the manifest; user/runtime data (incl. granular sample buffers) → a `value`/stream endpoint the host writes.

- **Perf — the per-node micro-benchmark is misleading; the realistic shape is ~1.3×.** Equal-work, 512 channel-streams @ 48 kHz (correcting Spike I's mono-vs-stereo asymmetry, which flattered the 1.03× linear figure):
  - **Single mono node** (`NlSvf`): copy adapter **1.95×**, zero-copy lean adapter **1.85×** vs hand-written C++.
  - **Fused per-voice chain** (`NlSvfDrive` = SVF + saturator + drive in *one* processor): **~1.27–1.32×**, and **flat across block sizes 64→512**.
  - **Interpretation:** the gap is **per-sample generated-code cost (~30%)**, *not* per-advance overhead (block-size-independent) and *not* adapter glue (zero-copy buys only ~5%). It **amortizes as DSP-per-processor grows** — fusing two stages already takes 1.95×→1.3×, so a realistic v6 per-voice graph authored as **one fused Cmajor processor** (many nodes per advance) lands ~1.3× or better. The per-instance footprint is an 8-byte handle + the generated state on the heap.

**Net:** correctness and the data boundary are non-issues; the perf cost is a real but modest **~1.3× on the per-voice DSP** *provided the graph is authored as fused per-voice processors, not micro-nodes*. This confirms — and sharpens — the GO/hybrid recommendation below.

## Decision

**GO — adopt Cmajor as a coexistence DSP layer (hybrid), pending the nonlinear follow-up below.**

- **Boundary:** **JUCE owns** the host, UI, parameters, MIDI, preset/state, and the voice pool. **Cmajor owns** per-voice DSP block math (filters, shapers, oscillators, the v6 graph nodes) via AOT generated-C++ wrapped behind thin C++ adapters that present Bernie's existing interfaces (`FilterModel`-style). Bernie keeps its **C++ voice pool** — we do *not* move polyphony into a Cmajor patch (`std::voices::VoiceAllocator` is noted but not adopted).
- **v6 graph:** author the v6 per-voice DSP graph **in Cmajor**, generated to committed C++. This is the decision the gate exists for — it avoids the double-build. **Author it as fused per-voice processors (a whole chain per `advance`), not micro-nodes** — the Spike II addendum shows per-node granularity costs ~1.95× while a fused chain is ~1.3×. (The nonlinear follow-up this originally hinged on is now complete — see the Spike II addendum.)
- **Workflow:** `.cmajor` source + generated `.h` are committed together; regenerate only when the source changes; CI compiles plain C++ with no Cmajor dependency.
- **`cmajor-migration`:** reframed from "conditional/tentative" to a **planned hybrid adoption** whose first concrete step is porting a real shipping DSP block (not throwaway spike code) behind its existing interface.

Rationale: equivalence is **bit-exact** (linear *and* nonlinear — Spike II), integration is frictionless, the **data boundary is solved**, and the realistic per-voice perf is **~1.3×** when authored as fused processors (Spike II) — an acceptable tax for the double-build avoidance, graph-authoring fit, and retargetability. The licensing position is clean. The debits — a ~1.3× DSP CPU cost (budget it into the 256-voice perf gate; fuse nodes to contain it), a per-voice heap allocation, and dev-time Docker codegen — are bounded and none touches the shipping binary or CI.

## Consequences

**Commits us to:** authoring v6's graph DSP in Cmajor; maintaining the codegen-commit workflow ([`cmaj-codegen.sh`](../../tools/cmajor/cmaj-codegen.sh) + committed generated C++); a thin-adapter discipline so generated specifics never leak past one wrapper per block.

**Avoids:** building the v6 graph engine twice; any GPL/commercial-license exposure (engine never embedded); any Cmajor dependency in CI or the shipping VST3/Standalone.

**Costs / follow-ups:**
- **Nonlinear follow-up spike — DONE (Spike II).** Both nonlinear stages port bit-exact; realistic fused perf ~1.3×. The remaining nonlinear unknown is **oversampling** cost (not piloted) — measure when v5.3 HQ tiers land, since oversampled nonlinear nodes multiply the per-sample cost the ~1.3× rides on.
- **Author v6 as fused per-voice processors.** Per-node granularity is ~1.95×; a fused chain is ~1.3×. Budget ~1.3× DSP CPU into the 256-voice perf gate (thread-perf), and prefer one Cmajor processor per voice over many wired micro-nodes.
- **Per-voice allocation.** Replace the `unique_ptr`-to-heap adapter state with in-place storage before any shipping use (dovetails with the v5.1 heap→in-place `State` migration, register Q17/Q18).
- **Windows CI confirmation.** The committed generated C++ + adapters live in the test target, which Windows CI builds — confirm green on the next CI run (ordinary C++, expected trivial).
- **Toolchain ergonomics.** Optionally build a cached codegen image (the script apt-installs webkit each run) and document the codegen step for contributors.

**Supersedes:** the open question in [[feedback-cmajor-coexist-juce]] ("where does the JUCE↔Cmajor boundary sit") — answered above: Cmajor below the per-voice DSP-block line, JUCE everywhere else.
