# Runtime Analysis (ASan/UBSan) — Memory Safety

**Version:** 5.12 · **Date:** 2026-07-02 · Part of the [codebase health audit](README.md).

> **STATUS: FIXED same day.** `Layer::prepare` now creates `models_` once and
> re-prepares in place (stable instances); contract test added in
> `LayerTests.cpp` ("stable across re-prepare"). Full ASan/UBSan suite re-run:
> **262 tests, 0 failed, zero sanitizer reports** — this is the project's
> memory-safety baseline going forward. The analysis below is kept as the
> record of the defect and its mechanism.

## Setup (reproducible)

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan --target k2000_tests -j4
./build-asan/tests/k2000_tests
```

## P0 — heap-use-after-free in `SpineFilterSlot::prepare`

**Detected at:** `src/dsp/spine/SpineFilterSlot.cpp:18`, triggered by
`PluginLifecycleTests.cpp:94` (`setRealtimeOS`) — i.e. the plain unit suite
already exercises the crash path; it "passes" un-sanitized only because the
freed memory happens to still be readable.

**Mechanism (three actors):**

1. `Layer::models_` owns the `FilterModel` instances (`std::vector<std::unique_ptr<FilterModel>>`).
2. Each voice's `SpineFilterSlot` caches **non-owning** `model_[2]` pointers to
   those instances (it needs them to `destroyState()` the in-place per-voice
   state they constructed).
3. `Layer::prepare` (Layer.cpp:12) does `models_.clear()` **and recreates the
   vector on every prepare** — deleting the old instances.

`K2000AudioProcessor::reprepareForOS()` calls `program_.prepare(...)` (frees
the models) **then** `voiceManager_.prepare(...)` → `Voice::prepare` →
`SpineFilterSlot::prepare`, whose cleanup loop dereferences the now-dangling
`model_[i]` to destroy the old state:

```cpp
for (int i = 0; i < 2; ++i)
    if (state_[i]) { model_[i]->destroyState(state_[i]); ... }   // ← UAF
```

**Production triggers:** any oversampling-factor change (`setRealtimeOS` /
`setOfflineOS` from the OS menu) and every Live↔Offline transition (first
render block, first live block after a render) — whenever a voice slot holds
constructed state. This is not test-only.

**Related latent hazard:** `~SpineFilterSlot()` has the same shape. It is
currently safe only because `voiceManager_` is declared after `program_` in
the processor (destroyed first) — an undocumented ordering dependency.

## Recommended fix

Make the model instances **stable for the Layer's lifetime**: in
`Layer::prepare`, create `models_` only if empty; otherwise call
`m->prepare(sr)` on the existing instances. This removes the entire
stale-pointer class (slots, `huggett_`/`moog_` cached views, and the
destructor ordering dependency all become safe), and the hot-swap crossfade
logic is untouched. Alternative (weaker): tear down all voice slots before
recreating models — preserves the recreate but keeps the ordering trap.

Regression test: a `PluginLifecycle` case that calls `setRealtimeOS` twice
with an active note, run under ASan in CI (or at minimum documented in the
ASan runbook above).

## Coverage caveat (P1)

`-fno-sanitize-recover=all` aborts at the first finding, so **everything after
`PluginLifecycle` in the run order is unverified** under ASan/UBSan. After the
P0 fix, re-run the full suite sanitized; treat a clean full pass as the actual
memory-safety baseline. No UBSan findings surfaced before the abort point.

Also observed (pre-existing, benign): `juce_Timer.cpp:376` and
`juce_Singleton.h:62` assertion noise on stderr during shutdown of the
unsanitized suite — cosmetic, but worth silencing during the anti-drift
harness work so real assertions stand out.
