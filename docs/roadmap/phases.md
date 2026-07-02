# Phase plan

> **The live roadmap — every version, status, ordering, point release, and task —
> lives in [`tools/roadmap-dashboard/roadmap.json`](../../tools/roadmap-dashboard/roadmap.json)
> and is viewed/edited via the roadmap dashboard:**
>
> ```bash
> cd tools/roadmap-dashboard && npm install && npm run dashboard   # http://localhost:4173
> ```
>
> This document holds only the **durable vision and engine principles** — the things
> that do not change release to release. It deliberately carries **no** status tables,
> so it can never drift from the dashboard. (Brought into this form 2026-06-20; see the
> [dashboard design spec](../superpowers/specs/2026-06-20-roadmap-dashboard-design.md).)

## End-state vision (re-positioned to K2061/K2088, 2026-06-16)

The plugin is a **K2061/K2088-class VAST engine bracketed by a constant Summit analog
voice.** Sound *generation* is fully flexible (K2061 Dynamic VAST); sound *shaping* is
always a Summit.

- **Flexibility from K2061/K2088 VAST** — Dynamic VAST: build sound from arbitrary
  serial/parallel DSP graphs where every source (Summit oscillators, KVA, FM, wavetable,
  noise — and later samples) is just a block. 32 layers, Multis, KDFX, Cascade.
- **A constant Summit voice** — a **selectable, live-switchable filter model** (Huggett
  default; Moog and Oberheim SEM later) + drive → VCA, and the modulation system
  (amp/mod envelopes, LFOs, mod matrix, voice modes), are **always present** and always
  live. You can never reach a dead control or a patch that isn't a real synth.
- **Immediacy from Summit** — the constant spine is the permanent front panel; the
  variable source/DSP region's knob-clusters swap to match the active blocks. Tiered
  immediacy: front panel for live params, pages for the long tail.

See the [v4.5(C) re-positioning spec](../specs/2026-06-16-v4.5-k2061-repositioning-design.md)
and the living [engine architecture register](../architecture/engine-questions.md).

## Product naming

The shipping synthesizer is **Bernie** (repo codename `k2000` stays). Bernie's built-in
effects section is **Ricky** — a Summit/KDFX-style multi-FX block reached via an
*Advanced* button on Bernie's front panel (Arturia-style), inserted **after the amp/VCA**,
with a subset of its FX blocks also exposed as VAST DSP blocks (roadmap item **v8**).

## Engine principles (cross-cutting)

- **The model:** `[ K2061 Dynamic VAST source + DSP graph (variable) ] → [ constant
  Summit spine: selectable filter model (Huggett default) + drive → VCA, with
  envelopes/LFOs/mod matrix/voice modes ]`, **per voice, per Layer**.
- **Locked decisions** (from the register): **full stereo throughout** · **64–128-voice**
  target (re-resolved 2026-07-02 from 256 with measured per-voice cost — register Q2/Q23) ·
  spine + modulation **per-Layer** · **synth-only** sources now (sample/keymap arrive later).
- **GUI grows with the engine, toward a fixed aesthetic** — no phase ships a feature you
  can't drive; each phase advances the visual design incrementally toward the target
  Summit aesthetic rather than deferring a "real GUI" to the end.
- **Performance is a gate** — at 64–128 voices × full stereo × graph DSP, every phase meets a
  per-voice CPU budget with profiling as a release gate (first real numbers: register Q23).

## Cross-cutting threads

These are continuous, not version-pinned (tracked in the dashboard's "Continuous
threads"): the **DSP test harness** (grows to cover every component, gates releases) · the
**per-voice perf gate** · the **incremental GUI** toward the target aesthetic · the
**security-scan CI baseline**.

## Cmajor — a decision gate, not a version

Cmajor (graph-based DSP language) is evaluated as a **spike before v6**: pilot one filter
model, verify JUCE integration, prove the 256-voice per-voice model, then write an ADR.
The full migration's position is decided by that spike — if it wins, the v6 graph is
authored in Cmajor (avoiding a double build); if not, the C++ DSP stays. This is why the
spike must resolve **before v6 is designed**.

## What this is *not*

- A commitment. Ordering can shift if a downstream phase reveals an upstream one was
  over- or under-scoped. The dashboard's `firmness` field marks v12+ as tentative.
- A deadline. No dates on unshipped work.
- Permission to scope-creep an earlier phase. Each version only ships what it scopes.

## How a phase becomes real

1. Enumerate the phase's open questions into the
   [engine architecture register](../architecture/engine-questions.md); ask them; record
   answers; **groom the register for internal consistency**.
2. Write `specs/YYYY-MM-DD-vN-<theme>-design.md` once the relevant questions are resolved.
3. Capture non-obvious decisions as ADRs in [`../decisions/`](../decisions/).
4. Add architecture deep dives in [`../architecture/`](../architecture/) for load-bearing
   subsystems.
5. Update the phase's status in the dashboard (`roadmap.json`).
