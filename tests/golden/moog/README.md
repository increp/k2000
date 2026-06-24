# Moog Ladder golden data — Arturia Mini V capture format

Place Arturia Mini V measurement exports here so the golden-data harness in
`tests/MoogLadderTests.cpp` can verify the model against a hardware reference.

## response.csv

Frequency-response sweep: one row per probe point.

```
cutoffHz,resonance,probeHz,magDb
```

- `cutoffHz`  — filter cutoff frequency in Hz (e.g. 1000.0)
- `resonance` — normalised resonance in [0, 1] (e.g. 0.0 = off, 0.9 = high)
- `probeHz`   — sine probe frequency in Hz
- `magDb`     — measured output level relative to input, in dB (e.g. -3.01)

Header row is optional; any row whose first token contains a non-numeric
character is skipped by the harness.

Example rows:

```
1000.0,0.0,100.0,0.12
1000.0,0.0,1000.0,-3.01
1000.0,0.0,8000.0,-45.3
```

## selfosc.csv

Self-oscillation pitch-tracking sweep: one row per cutoff setting.

```
cutoffHz,measuredHz
```

- `cutoffHz`   — cutoff set on the hardware (resonance = max)
- `measuredHz` — zero-crossing rate measured from the captured audio output

Example rows:

```
220.0,220.4
440.0,439.8
880.0,881.2
```

## Tolerance

The harness currently uses a ±6 dB CALIB band (`< 6.0` in the `expect` call).
Tighten the constant in `MoogLadderTests.cpp` once real data has been captured
and the model calibrated.

## Workflow

1. Capture Arturia Mini V output for each (cutoffHz, resonance, probeHz) combo.
2. Compute the dB level of the output relative to the input sine amplitude.
3. Write rows into `response.csv` (no header required).
4. Run `./build/tests/k2000_tests` — the golden test will activate.
