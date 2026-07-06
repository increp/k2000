# Franklin — Charter

**Version:** 5.21 (artifact; distinct from plugin SemVer — see `CLAUDE.md` process rails)
**Date:** 2026-07-03

> "Franklin is what keeps us honest and our synths musical."
> — user, 2026-07-03

Bernie makes the sound. Ricky colors it. **Franklin measures both.**

Franklin is this repo's measurement/validation product: the device-characterization
core, the per-model filter and oscillator profiles, the hardware bridge that
fingerprints real analog gear, the harness that captures external VSTs for
comparison, the suite gates and drift rules that keep the whole build honest, and
the live dashboard that lets a human watch any of it run. Where Bernie and Ricky
exist to be played, Franklin exists to be trusted — every claim this project makes
about fidelity, regression safety, or hardware match traces back to a Franklin
measurement.

**Franklin is held to an instrument-grade bar — it is the trust anchor for every
external comparison.** As Franklin grows to measure commercial VSTs and real
hardware and to host ours-vs-theirs comparisons, its own correctness is load-bearing:
a shallow or flaky harness poisons every downstream authenticity decision. Changes to
Franklin are reviewed as instrument code, not UI code — forward-compatible schemas,
full backfills over partial ones, a drift rule behind every catalog-like artifact,
and a live smoke before merge.

This page is a map of Franklin's remit, not a status board. Progress on any item
below lives in the roadmap dashboard (`cd tools/roadmap-dashboard && npm run
dashboard`), never here.

## Remit map

| Franklin component | Real home |
|---|---|
| SP-A — device-characterization core | `docs/superpowers/specs/2026-07-01-device-characterization-core-design.md` |
| SP-B — filter profile | register [Q27](../architecture/engine-questions.md), `docs/reviews/2026-07-02-huggett-large-signal-read.md` |
| SP-C — oscillator profile | register [engine-questions.md](../architecture/engine-questions.md) (scope tracked alongside SP-B) |
| SP-D — hardware bridge + Summit excitation risk | register [Q25](../architecture/engine-questions.md) |
| External-VST capture harness | roadmap item "v5 — External VST test harness" (`tools/roadmap-dashboard/roadmap.json`, id `v5-vst-test-harness`) |
| Future effect batteries (drive/saturation/distortion/chorus) | Ricky's FX section — scope lives in the register + roadmap when raised |
| Suite gates + drift rules | `tools/drift-check` |
| The runs dashboard | `docs/franklin/dashboard.md` |

## Naming

Franklin is the instrument, not a feature of it. **Bernie** is the synth, **Ricky**
is the FX section, **Franklin** is what characterizes, validates, and reports on
both. `k2000` remains the internal repo codename only (register L6); none of the
three product names replace it.

## See also

- `docs/franklin/dashboard.md` — how to run and read the live runs dashboard.
- `docs/architecture/engine-questions.md` — the living register (row L8 records this
  naming ruling).
- `docs/superpowers/specs/2026-07-03-franklin-dashboard-design.md` — the dashboard's
  design spec.
