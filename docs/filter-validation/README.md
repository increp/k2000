# Filter-Validation Harness

**Version:** 5.11
**Date:** 2026-07-02

A model-agnostic, dual-method (stepped-sine + Farina ESS) internal
filter-characterization harness that produces numbers-only measurements of each
filter model against the textbook ideal for its type. Every frequency-response
point is cross-checked by two independent measurement paths; the results must
agree within a tight tolerance before any spec assertion is evaluated. This is
sub-project 1 (internal/textbook-ideal correctness). External comparison against
real analog hardware (Arturia Mini V for Moog; Summit hardware for Huggett) is
sub-project 2 and is not built yet.

---

## 60-Second Quickstart

### Always-on gate (every build — ~5 sec overhead)

The `CharacterizationGate` tests run automatically inside the standard test binary.
They exercise a small fixed grid (Moog + Huggett, LP24 at fc 1000 Hz, OS factor 1)
and assert slope, method-agreement, and self-golden fingerprint checks.

```
cmake --build build --target k2000_tests -j4
./build/tests/k2000_tests
```

Expected: `Summary: 298 tests, 0 failed`

### Opt-in fingerprint (on demand — 10-25 min for `--quick`, many hours without)

The `k2000_device_characterization` binary is not run in CI. Launch it deliberately
for deep characterization work. Results land in `build/characterization/<model>/`.

```
cmake --build build --target k2000_device_characterization -j4
./build/tests/k2000_device_characterization --model moog --quick
```

Exit code: 0 if all in-band `method_delta_db` values are < 1.0 dB (PASS), 1 otherwise.
The `--quick` flag selects the bounded coarse grid (~72 B1 points); omitting it selects
the full dense grid (~36 000 B1 points — use only for a deliberate production run).

---

## Pages in This Manual

| Page | Purpose |
|---|---|
| [concepts.md](concepts.md) | What the harness proves; the dual-method ruler; the three gates; the four batteries |
| [operating-points.md](operating-points.md) | Parameter axes, `coarseGrid` / `fullGrid` specs, `--quick` flag, aliasing isolation probe |
| [interpreting-results.md](interpreting-results.md) | Every CSV column and summary key; exit code semantics |
| [running.md](running.md) | Step-by-step build and run instructions; golden baseline refresh; Windows CI |
| [extending.md](extending.md) | Adding a new filter model: factory function, Configurator contract, smoke test, golden commit |
| [troubleshooting.md](troubleshooting.md) | Known output noise, degenerate metrics, gate failures, MSVC-specific pitfalls |
| [acceptance-criterion.md](acceptance-criterion.md) | When the framework is trusted for authenticity judgments (the §5.3 criterion, trust ladder, tolerances) |
