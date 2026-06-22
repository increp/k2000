# Cmajor Spike II — Nonlinear DSP + Data-Boundary (Design)

**Version:** 5.03 (artifact; distinct from plugin SemVer)
**Date:** 2026-06-21
**Status:** Approved (brainstorm) — pending spec review
**Roadmap item:** `cmajor-spike` follow-up (de-risks ADR-0012 ratification). Amends **ADR-0012**.
**Branch:** `feat/cmajor-spike` (continues Spike I).

---

## 1. Purpose & premise

[Spike I](2026-06-21-cmajor-spike-design.md) proved the **linear** TPT SVF ports to Cmajor with exact equivalence (45/45 within 0.5 dB) and CPU parity (1.03×, ~3% slower) through the AOT generated-C++ path, and slots cleanly into `FilterModel`. ADR-0012 is **Proposed (GO/hybrid)** on that evidence but left two questions open that gate ratifying "author v6's DSP graph in Cmajor":

1. **Does *nonlinear* per-voice DSP hold up?** Nonlinear feedback + waveshaping (with transcendental functions and a closed resonance loop) is where a perf or fidelity surprise would surface — the linear pilot can't speak to it.
2. **Can array/sample data cross the JUCE↔Cmajor boundary?** Spike I only moved *audio streams*. Wavetables (v7/v19) and granular (v19) need to hand a *table/buffer* to the generated class — unconfirmed under the no-runtime AOT model.

This spike answers both with bounded, throwaway-friendly probes. Same constraints as Spike I: **AOT generated-C++ only** (no Cmajor engine/JIT/runtime, ever), **commit the generated C++**, **test-target-only** (never in the plugin binary or `FilterModelLibrary`). A negative result is a fully successful spike outcome.

---

## 2. Decisions (resolved in brainstorm, 2026-06-21)

- **Port BOTH nonlinear flavors.** The in-loop **resonance saturator** (inside the SVF's closed solve — the hard, representative case) AND the feedforward **drive shaper** (`AsymSaturator` — the audible "warmth," and the path that exercises Cmajor's real `tanh`/`pow`).
- **Equivalence metric → per-sample primary, harmonic fallback.** Assert a tight per-sample max-abs-error against the C++ baseline (proves the port is arithmetically faithful AND measures whether Cmajor's generated float math matches ours). Where the closed loop + `tan()`/`tanh()` last-bit drift makes a strict per-sample bound flaky, fall back to a harmonic-amplitude comparison (musically meaningful, robust to inaudible numerical wobble). The achieved tightness is itself ADR evidence.
- **Data-boundary probe → a tiny wavetable oscillator.** Not a bare mechanism test — build a minimal single-cycle wavetable osc so the *real feature path* (push a table from C++ → interpolate → play) is proven end-to-end. De-risks wavetables and granular at once.
- **Chase the 3% with a lean/zero-copy adapter.** Add a lean adapter variant and re-bench 256 voices to determine whether the ~3% from Spike I is removable adapter glue or structural to the generated IO model. **In-place voice state is deferred** (a post-decision adoption task overlapping v5.1's heap→in-place work).
- **Modular structure (Approach A).** Three independent Cmajor modules, each with its own adapter + equivalence test, so a failure pins exactly one cause. Reject the combined-cell (couples nonlinearities) and full-`HuggettFilter` port (a feature, not a spike).

---

## 3. Architecture & files

Everything isolated under `src/dsp/spine/cmajor/`; the shipping plugin is untouched. Generated C++ is committed (regenerated only when the `.cmajor` changes) via the Spike I container path: `sg docker -c "tools/cmajor/cmaj-codegen.sh <patch.cmajorpatch> <out.h>"`.

```
src/dsp/spine/cmajor/                       (TEST target only)
  NlSvf.cmajor / .cmajorpatch               SvfLinear + in-loop resonance saturator (Padé tanh)
  AsymDrive.cmajor / .cmajorpatch           memoryless comp*tanh(gain*x+bias) shaper
  WtOsc.cmajor / .cmajorpatch               single-cycle wavetable oscillator (data-boundary vehicle)
  generated/NlSvf.h, AsymDrive.h, WtOsc.h   committed cmaj generate --target=cpp output
  NlSvfAdapter.{h,cpp}                       stable wrapper (copy path) over NlSvf
  NlSvfLeanAdapter.{h,cpp}                   lean/zero-copy variant (chase the 3%)
  AsymDriveAdapter.{h,cpp}                   stable wrapper over AsymDrive
  WtOscAdapter.{h,cpp}                       stable wrapper; setTable(const float*, int) hides the data-in mechanism
tests/
  NlSvfEquivalenceTests.cpp                  per-sample (+ harmonic fallback) vs NlSvfCell(resSat>0)
  AsymDriveEquivalenceTests.cpp              per-sample (+ harmonic fallback) vs AsymSaturator
  WtOscTests.cpp                             table pushed from C++ -> spectrum/peak correct
  NlSvfPerfTests.cpp                         256-voice: copy vs lean adapter vs NlSvfCell(resSat)
docs/decisions/0012-*.md                     AMENDED with Spike II findings
```

**Module specifics:**

- **`NlSvf.cmajor`** mirrors [NlSvfCell::step](../../src/dsp/spine/NlSvfCell.h): linear TPT core (from Spike I) + per-channel `bp` state (previous BP output) + the resonance-loop correction `v0 -= k * resSat * (satRes(bpPrev) - bpPrev)`, where `satRes()` is the asymmetric normalized **Padé 3/2** tanh (`padTanh`, `padTanhDeriv`) — pure +,*,/,clamp, so per-sample equivalence should be tight (the only transcendental is `tan()` once per coefficient recompute). Params: cutoff, resonance, resSat, tap.
- **`AsymDrive.cmajor`** mirrors [AsymSaturator](../../src/dsp/spine/AsymSaturator.h): `g(x) = comp * tanh(gain*x + bias)`, computing `gain = 10^(drive01*maxDriveDb/20)`, `comp = 1 + 0.75*(1/tanh(gain) - 1)` (when gain>1), `bias = biasFixed` **inside Cmajor** (so the port genuinely exercises Cmajor's `pow`/`tanh`, not C++-precomputed coefficients). Params: drive01, biasFixed, maxDriveDb.
- **`WtOsc.cmajor`**: a single-cycle table of fixed max size, a phase accumulator from a frequency input, linear interpolation between table samples. The table is supplied from C++ (mechanism per §4).

**Adapter contracts** (generated-API hidden, the Spike I pattern):
```cpp
class NlSvfAdapter {     // + NlSvfLeanAdapter with identical interface
  void prepare(double sr); void reset();
  void setParams(float cutoffHz, float resonance, float resSat, int tap);
  void process(float* mono, int n);
};
class AsymDriveAdapter {
  void prepare(double sr); void reset();
  void setParams(float drive01, float biasFixed, float maxDriveDb);
  void process(float* mono, int n);
};
class WtOscAdapter {
  void prepare(double sr); void reset();
  void setTable(const float* table, int n);   // hides external-vs-stream/event data-in
  void setFrequency(float hz);
  void process(float* mono, int n);
};
```

---

## 4. The funnel (build sequence + gates)

Ordered to kill the spike cheaply. Build with `-j4`, never bare `-j`; run `./build/tests/k2000_tests`.

1. **`NlSvf` port + equivalence.** Extend Spike I's SVF with the resonance saturator; generate + commit. Bench per-sample vs `NlSvfCell` (resSat>0) across cutoff×resonance×level. **Gate:** per-sample within an empirically-set bound (record it); else harmonic-amplitude match within tolerance (record why). A real divergence pins a porting bug — fix the `.cmajor`, regenerate.
2. **`AsymDrive` port + equivalence.** Generate + commit. Bench vs `AsymSaturator` across drive levels. **Gate:** per-sample-then-harmonic match; explicitly record the `tanh`/`pow` last-bit behavior (Cmajor intrinsic vs `std::`) — primary evidence on whether transcendental-heavy DSP ports faithfully.
3. **Data-boundary discovery + `WtOsc`.** First generate a probe patch with an `external float[]` and **inspect the generated C++** to determine how externals surface: baked-at-codegen vs runtime-settable. If not runtime-settable, use an input stream/event to load the table. `WtOscAdapter::setTable()` hides the winner. **Gate:** a table pushed from C++ produces the expected single-cycle output (spectrum/peak); record the mechanism (the reusable wavetable/granular answer).
4. **Lean/zero-copy adapter + 256-voice perf.** Implement `NlSvfLeanAdapter` minimizing copies/indirection to the extent the generated API allows. Bench 256 voices: copy adapter vs lean adapter vs `NlSvfCell`(resSat); print all ratios + `sizeof`s. **Gate:** numbers (no hard pass/fail) — and an explicit verdict on whether the ~3% is removable or structural.
5. **Amend ADR-0012.** Fold in: nonlinear equivalence result (both stages, the achieved tightness/metric), the data-boundary mechanism, the lean-adapter perf verdict. Update the recommendation's confidence; the user ratifies (Proposed→Accepted) or not.

---

## 5. Testing

All gates are JUCE `UnitTest`s in the existing `tests/` suite, test-target-only. Equivalence tests hard-assert (CI-failing). The perf bench prints ratios + sizeofs and asserts only finiteness/sanity (a decision input, not a pass/fail). Per-sample equivalence uses an identical input signal fed to both paths; the harmonic fallback compares harmonic amplitudes of a steady tone. Reuse `tests/testdsp` helpers where applicable. The committed generated C++ + adapters + tests build in Windows CI as ordinary C++ (no Cmajor toolchain in CI) — same as Spike I.

---

## 6. Out of scope (YAGNI)

- The full `HuggettFilter` (two cells + DC blocker + routing + series/parallel), oversampling, the dual-section separation.
- A granular engine (the wavetable osc is the *data-boundary* probe, not a granular feature).
- In-place / heap-free voice state (deferred adoption task; overlaps v5.1 heap→in-place).
- Registering anything in `FilterModelLibrary`; any plugin-binary inclusion; any Cmajor runtime/engine dependency; build-time codegen in CI.
- A `FilterModel` wrapper for the nonlinear cell (Spike I already proved the `FilterModel` slot-in; not re-litigated here).

---

## 7. Success criteria

The spike succeeds when ADR-0012 can be amended with: a measured nonlinear equivalence result for **both** stages (with the metric/tightness achieved), a concrete data-boundary mechanism (external vs stream/event) proven by a working wavetable osc, and a 256-voice perf verdict on whether the ~3% is removable. A negative finding (nonlinear too divergent, data boundary impractical, or perf structurally worse) is a fully successful outcome that would downgrade the GO/hybrid recommendation — that is exactly the de-risking this spike exists to do.
