# Filter-Validation Harness — Adding a New Filter Model

**Version:** 5.01
**Audience:** Audio engineer adding a new filter model (e.g. a Buchla, a CS-80 section, a custom SVF).

---

## Overview

The harness is model-agnostic: every filter is accessed through the `chz::FilterUnderTest` socket.
Adding a model means writing one factory function (`makeXFut()`) with a model-specific `Configurator` lambda.
The runner (`chz::CharacterizationRunner`) and all batteries (B1–B4) work unchanged.

---

## Step 1: Write the factory function

Add a declaration to `tests/characterization/FilterUnderTest.h`:

```cpp
std::unique_ptr<FilterUnderTest> makeBuchlaFut();
```

Add the definition to `tests/characterization/FilterUnderTest.cpp`:

```cpp
std::unique_ptr<FilterUnderTest> makeBuchlaFut() {
    auto cfg = [](FilterModel& fm, Mode m) -> bool {
        auto& buch = static_cast<BuchlaFilter&>(fm);
        switch (m) {
            case Mode::LP12: buch.setSlope(12); return true;
            case Mode::LP24: buch.setSlope(24); return true;
            case Mode::BP:   buch.setMode(BuchlaFilter::BP); return true;
            // Return false for any Mode the model does not support.
            // Example: Moog ladder returns false for Notch because the
            // 4-pole cascade topology has no notch output tap.
            case Mode::HP:    return false;
            case Mode::Notch: return false;
        }
        return false;
    };
    return std::make_unique<FilterUnderTest>(
        "buchla",                       // name used in summary keys + CSV
        std::make_unique<BuchlaFilter>(),
        cfg);
}
```

### Configurator contract

The `Configurator` is `std::function<bool(FilterModel&, Mode)>`.

- **Return `true`** if the model supports the requested mode. Reconfigure `fm` before returning (call `setMode`, `setSlope`, `setRouting`, etc.).
- **Return `false`** for modes the topology does not produce (the runner skips that mode silently). Example: `makeMoogFut()` returns `false` for `Mode::Notch`.
- The configurator is called both for capability probes (`fut->supports(m)`) and at the start of each measurement. Both cases share the same lambda — keep it side-effect-safe (it may be called without a following measurement).

---

## Step 2: Which Modes to support

Declare only the modes your model's physical topology can produce.
Use the `chz::Mode` enum:

| `chz::Mode` | Typical topology requirement |
|---|---|
| `LP12` | Single-pole-pair LP output tap |
| `LP24` | Two-pole-pair LP output tap |
| `BP`   | Band-pass output tap |
| `HP`   | High-pass output tap |
| `Notch`| Notch/band-reject output tap — Huggett yes, Moog no |

Do not return `true` for a mode whose output tap does not exist. The runner treats `false` as "skip" — no measurement, no CSV row, no summary key.

---

## Step 3: Add a smoke test (model-agnosticism gate)

Before committing, add a subtest in `tests/CharacterizationRunnerTests.cpp` that runs a **tiny grid** through your new FUT and asserts that at least one expected summary key is present. This proves the runner socket is genuinely model-agnostic (no residual Moog-shaped assumption in `CharacterizationRunner` or `FilterUnderTest`).

Example pattern (mirrored from the Huggett back-test):

```cpp
beginTest("Buchla runs the battery (model-agnostic socket)");
{
    auto fut = chz::makeBuchlaFut();
    chz::Grid g;
    g.modes = { chz::Mode::LP24, chz::Mode::BP };
    g.cutoffs = { 1000.0 }; g.resonances = { 0.0 }; g.drives = { 0.0 };
    g.osFactors = { 1 }; g.osModes = { chz::OsMode::Live }; g.hostRates = { 96000.0 };
    g.probeFreqs = chz::CharacterizationRunner::logFreqs(50.0, 20000.0, 40);

    auto outDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("chz_buchla_test");
    outDir.deleteRecursively(); outDir.createDirectory();
    auto summary = chz::CharacterizationRunner::run(*fut, g, outDir);

    expect(summary.count("buchla/LP24/fc1000/corner_hz") == 1, "Buchla LP24 corner present");
    outDir.deleteRecursively();
}
```

If this test **fails or crashes**, the runner/socket has a model-specific assumption that needs fixing — do not paper over it.

---

## Step 4: Generate and commit the self-golden baseline

Once the smoke test passes, generate the model's full-grid baseline CSV:

```bash
./build/tests/characterize_main --model buchla --grid coarse --out baselines/buchla/
```

Commit the baseline files under `baselines/buchla/`. Future runs compare against this baseline via the `BERNIE_UPDATE_GOLDEN` CI workflow (Task 12 forward-reference). See the workflow dispatch docs for details on running the baseline-update job.

The self-golden baseline does not claim hardware accuracy — it fixes the model's own behaviour so regressions are caught. External accuracy against the real analog device is sub-project 2.

---

## Summary checklist

- [ ] `makeXFut()` declared in `FilterUnderTest.h`
- [ ] `makeXFut()` defined in `FilterUnderTest.cpp` with correct Configurator
- [ ] Only supported `Mode` values return `true` from the Configurator
- [ ] Smoke test added to `CharacterizationRunnerTests.cpp` and passing
- [ ] `./build/tests/k2000_tests` shows 0 failed
- [ ] Baseline CSVs generated and committed under `baselines/<model>/`
