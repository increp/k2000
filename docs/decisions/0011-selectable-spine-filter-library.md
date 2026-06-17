# ADR 0011 — Selectable spine filter: a curated library of analog filter models

**Status:** Accepted, 2026-06-17. **Version:** 5.01. Effective from v5 (library + Huggett); models added v5.1+.

## Context

[ADR-0010](0010-k2061-repositioning-constant-summit-spine.md) made the constant Summit spine "the **Huggett** multimode filter + drive → VCA." In practice the spine slot *is* the instrument's signature analog-shaping stage — the one filter every voice always passes through. There is a family of legendary, **legally-clear** analog filters that belong in that slot, not just one:

- **Huggett** (OSCar → Peak → Summit) — dual state-variable OTA, the flagship ([huggett-filter.md](../architecture/huggett-filter.md), [dossier](../architecture/huggett-filter-dossier.md)). **Unpatented.**
- **Moog transistor ladder** — 4-pole, self-oscillating ([moog-ladder.md](../architecture/moog-ladder.md), [SEM/Moog dossiers](../architecture/filter-dossiers-sem-moog.md)). **Patent (US 3,475,623) expired 1986.**
- **Oberheim SEM** — 2-pole multimode SVF, non-self-oscillating, LP→notch→HP morph ([dossier 1](../architecture/filter-dossiers-sem-moog.md)). **Unpatented** (KHN topology is public-domain academic work).
- **Others later** (diode ladder, etc.).

The user's intent: *all of these available in the signature default Huggett slot.* That is a swappable-model library, not a Huggett-only stage.

## Decision

The spine filter is a **curated, append-only `FilterModel` library** — the [ADR-0008](0008-algorithm-selection-and-param-namespace.md) stable-ID idiom applied to the spine:

- **Selection:** an **automatable** per-Layer `spine.filterModel` choice; **Huggett is the flagship default.**
- **Each entry** is a **behavior-faithful (gray-box) analog model** implementing the `FilterModel` interface (heap-free per-voice state, stereo). Adding a model = append one library entry + one class; stable indices keep presets safe.
- **Live click-free switching** via a per-voice equal-power **crossfade** of two in-place model instances (register **L7**, Q17–Q19).
- **Params:** a **common core** (cutoff/resonance/drive/output — always front-panel + mod-targetable) + **per-model namespaced banks** `spine.<modelId>.<param>`.
- **Legal posture:** only **unpatented or patent-expired** topologies are replicated, **behaviorally** (no brand names in IDs beyond descriptive use, no factory presets, no copyrighted assets). SEM unpatented; Moog expired 1986; Huggett unpatented (see dossiers).

### Filter roadmap

| Model | Phase | Character |
|---|---|---|
| Huggett (dual SVF) | **v5** | flagship default; 12/24 dB, dual routings, three asymmetric drive stages |
| Moog ladder | **v5.1** | 4-pole, self-oscillating, per-stage tanh + oversampling |
| Oberheim SEM | **v5.2** | 2-pole, non-self-oscillating, LP→notch→HP morph + BP |
| others | later | e.g. diode ladder |

## Consequences

**Positive:** the signature slot gains breadth without losing identity (Huggett stays the default + the always-on drive/VCA/mod); extensible by construction; each model is grounded in dedicated research dossiers; topologies are legally unencumbered.

**Costs / follow-ups:** per-model param banks + swappable UI clusters; the live hot-swap machinery (Plan 3) and the per-voice "two slots sized to the largest model" budget (**Q18**); the dangling-pointer/state-type hazards on model change (register Q17 — already mitigated for the single-model path); per-model self-oscillation/drive **calibration against reference hardware**. Oversampling cost at 256 voices is gated by the perf gate (**Q11/Q12**) and differs per model (Moog needs the most; SEM the least; Huggett conditional on drive).

**Supersedes:** the Huggett-*only* reading of ADR-0010 / L2 — the spine is now "a selectable analog-filter voice, Huggett default."
