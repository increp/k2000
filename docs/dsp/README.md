# DSP notes

Math derivations, references, and tuning notes for the synthesis and processing code. This folder grows as DSP is implemented — empty at the v1 spec stage.

The point of these notes isn't to reproduce textbooks, it's to capture the specific choices the project's DSP makes and *why*, so that future-you (or a future collaborator) can re-derive or change them without spelunking through commit history.

## What goes here

A note when:
- A DSP block uses a non-obvious algorithm and the reader would otherwise have to read the implementation and reverse-engineer the math.
- Multiple algorithms exist for the same job and the choice between them matters (e.g. polyBLEP vs MinBLEP vs BLIT-SWS for anti-aliased oscillators; SVF vs ZDF vs Moog ladder for filter topologies; tanh vs cubic-soft vs hard-clip-then-LP for waveshapers).
- A coefficient came from somewhere specific (a paper, a measurement, a tuning session) and the source matters for trust or for reproducing it.

## What does *not* go here

- Generic DSP theory available in any textbook.
- Step-by-step implementation walkthroughs — the code itself should be readable.
- Tuning that's already documented inline as a comment in the source file.

## Anticipated notes (not yet written)

- **Oscillator anti-aliasing** — polyBLEP derivation, why we picked it over alternatives, when its alias floor stops being acceptable (probably around v7 when wavetables arrive).
- **SVF filter topology** — Andy Simper's state-variable formulation, why ZDF for v1, edge cases at high resonance.
- **Peak-style analog filter model (v2)** — references for the actual Peak filter topology, choice of model (Moog vs OTA-style multimode), tuning notes for matching the hardware character.
