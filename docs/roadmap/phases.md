# Phase plan

Order is suggestive, not binding. Phases can re-order based on what we learn during each one. Each phase gets its own spec in [`../specs/`](../specs/) before implementation begins.

| Phase | Theme | What lands |
|---|---|---|
| **v1** | Skeleton end-to-end | 1 oscillator, 2-slot VAST chain (SVF + waveshaper), ADSR amp, 8-voice polyphony, plain JUCE UI. Linux dev + Windows builds via CI. See [v1 spec](../specs/2026-05-25-v1-skeleton-design.md). |
| **v2** | Real Peak character | 3 oscillators per voice, Peak-style detune/tune controls, analog-modeled multimode filter (the signature Peak sound), pre/post-filter drive routing. |
| **v3** | Modulation | 2 LFOs, 2nd envelope (filter env), mod matrix (4–8 slots: source → destination → amount). |
| **v4** | VAST flexibility | User-selectable block type per slot; expanded block library (EQ, comb filter, ring mod, more shaper flavors); 3–4 slots per voice; multiple algorithms with different routing topologies. |
| **v5** | FX section | Chorus, delay, reverb on the master bus. |
| **v6** | Photoreal UI | Photo backdrop (Summit + K2000) + JSON layout file + transparent JUCE controls. Trademark/copyright question must be resolved before any public distribution. |
| **v7** | Wavetable / sample sources | Beyond polyBLEP — wavetable engine, optional sample playback (K2000 side). |
| **v8** | Preset browser, polish, cross-platform | Categorized preset library, macOS build target, scalable UI, performance polish. |

## What this is *not*

- A commitment. Phase order can shift if a downstream phase reveals that an upstream one was over- or under-scoped.
- A deadline. No dates here.
- Permission to scope-creep an earlier phase. v1 only ships v1.

## How a phase becomes real

1. When ready to start phase N, write `specs/YYYY-MM-DD-vN-<theme>-design.md` following the same shape as the v1 spec.
2. Capture the non-obvious decisions as ADRs in [`../decisions/`](../decisions/).
3. Add architecture deep dives in [`../architecture/`](../architecture/) for any subsystem that's load-bearing and non-trivial.
4. Update this file to mark the phase as in-progress, then completed.
