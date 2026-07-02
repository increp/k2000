#include "CharacterizationRunner.h"
#include "../testdsp/SteppedSine.h"
#include "../testdsp/EssResponse.h"
#include "../testdsp/MethodAgreement.h"
#include "../testdsp/Response.h"
#include "../testdsp/Harmonics.h"
#include "../testdsp/Level.h"
#include "../testdsp/Metrics.h"
#include "../testdsp/Spectrum.h"
#include "../testdsp/SignalGen.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace chz {

// ---------------------------------------------------------------------------
// logFreqs
// ---------------------------------------------------------------------------
std::vector<double> CharacterizationRunner::logFreqs(double f0, double f1, int n) {
    std::vector<double> v((size_t) n);
    if (n == 1) { v[0] = f0; return v; }
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = f0 * std::pow(f1 / f0, (double) i / (double) (n - 1));
    return v;
}

// ---------------------------------------------------------------------------
// coarseGrid — fast/CI: 96k, OS {1,2,4,8}, Live, coarse cutoff/res
// ---------------------------------------------------------------------------
Grid coarseGrid() {
    Grid g;
    g.hostRates  = { 96000.0 };
    g.osFactors  = { 1, 2, 4, 8 };
    g.osModes    = { OsMode::Live };
    g.modes      = { Mode::LP24, Mode::BP, Mode::HP };
    g.cutoffs    = { 250.0, 1000.0, 4000.0 };
    g.resonances = { 0.0, 0.9 };
    g.drives     = { 0.0 };
    g.probeFreqs = CharacterizationRunner::logFreqs(20.0, 24000.0, 200);
    return g;
}

// ---------------------------------------------------------------------------
// fullGrid — heavy: 5 host rates, OS x {Live,Render}, dense params
// ---------------------------------------------------------------------------
Grid fullGrid() {
    Grid g;
    g.hostRates  = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    g.osFactors  = { 1, 2, 4, 8 };
    g.osModes    = { OsMode::Live, OsMode::Render };
    g.modes      = { Mode::LP12, Mode::LP24, Mode::BP, Mode::HP, Mode::Notch };
    // 12 log-spaced cutoff points 50 Hz .. 16 kHz
    g.cutoffs    = CharacterizationRunner::logFreqs(50.0, 16000.0, 12);
    g.resonances = { 0.0, 0.3, 0.6, 0.9, 1.0 };
    g.drives     = { 0.0, 0.5, 1.0 };
    g.probeFreqs = CharacterizationRunner::logFreqs(10.0, 25000.0, 700);
    return g;
}

// ---------------------------------------------------------------------------
// interpMag — linear interpolation on the log-freq axis
// ---------------------------------------------------------------------------
double CharacterizationRunner::interpMag(const std::vector<double>& freqs,
                                          const std::vector<double>& magDb,
                                          double targetHz) {
    if (freqs.empty() || magDb.empty()) return std::numeric_limits<double>::quiet_NaN();
    const size_t n = std::min(freqs.size(), magDb.size());
    if (targetHz <= freqs[0])     return magDb[0];
    if (targetHz >= freqs[n - 1]) return magDb[n - 1];

    // Binary search for the bracket [lo, lo+1] surrounding targetHz.
    size_t lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        (freqs[mid] <= targetHz ? lo : hi) = mid;
    }
    // Linear interpolation in log-frequency space.
    const double logLo = std::log(freqs[lo]);
    const double logHi = std::log(freqs[hi]);
    const double logT  = std::log(targetHz);
    if (std::abs(logHi - logLo) < 1.0e-12) return magDb[lo];
    const double t = (logT - logLo) / (logHi - logLo);
    return magDb[lo] + t * (magDb[hi] - magDb[lo]);
}

// ---------------------------------------------------------------------------
// runB1OnePoint — single operating point: B1 (dual-method magnitude) + B4 (phase/gd)
//   Appends CSV rows to csvRows (caller assembles the full file).
//   Returns B1Result with corner_hz, slope_db_oct, method_delta_db.
// ---------------------------------------------------------------------------
CharacterizationRunner::B1Result CharacterizationRunner::runB1OnePoint(
        DeviceUnderTest& fut, const OperatingPoint& op,
        const std::vector<double>& probeFreqs,
        juce::String& csvRows) {

    const juce::String modelName = fut.name();
    const juce::String modeStr   = modeName(op.mode);
    const juce::String osModeStr = osModeName(op.osMode);

    // --- B1: Stepped-sine (reference) ---
    fut.setOperatingPoint(op);
    auto st = testdsp::SteppedSine::transfer(fut, probeFreqs, op.hostSampleRate, 0.05f);

    // --- B1 + B4: Calibrated ESS (Farina, referenced to identity baseline) ---
    // EssResponse handles reset() internally before driving the sweep.
    fut.setOperatingPoint(op);
    auto es = testdsp::EssResponse::measure(fut, 20.0, 24000.0, 1.0, op.hostSampleRate, 0.05f, probeFreqs);

    // --- method delta (in-band only) ---
    // Scope the stepped-vs-ESS comparison to within 40 dB of the passband peak. The deeper
    // stopband sits at both methods' noise floor, where a large delta is a measurement
    // artifact, not a real disagreement (measured: a Moog HP filter scatters ~5 dB at -60 dB
    // but agrees to <0.7 dB down to -40 dB). This makes method_delta_db a trustworthy gate.
    const double methodDelta = testdsp::MethodAgreement::maxMagDeltaDbInBand(st.magDb, es.magDb, 40.0);

    // --- -3 dB corner (from stepped-sine) ---
    // Passband reference strategy (brief §B1):
    //   LP/LP12: ref = magDb at the LOWEST probe freq (DC-adjacent; authentic Moog LP24
    //            has ~0.44*fc as the actual -3dB point when passband is anchored at DC,
    //            NOT at max, so using lowest-probe anchors the corner near fc correctly).
    //   HP:      ref = magDb at the HIGHEST probe freq.
    //   BP/Notch: ref = max(magDb) — peak-referenced (defined-but-approximate).
    double cornerHz = -1.0;
    {
        const size_t nPts = std::min(st.freqHz.size(), st.magDb.size());
        if (nPts >= 2) {
            double passbandDb;
            if (op.mode == Mode::LP12 || op.mode == Mode::LP24) {
                // Anchor at the lowest probe (DC-side passband).
                passbandDb = st.magDb[0];
            } else if (op.mode == Mode::HP) {
                // Anchor at the highest probe (high-freq passband).
                passbandDb = st.magDb[nPts - 1];
            } else {
                // BP / Notch: peak-referenced.
                passbandDb = *std::max_element(st.magDb.begin(),
                                               st.magDb.begin() + (ptrdiff_t) nPts);
            }
            const double threshold = passbandDb - 3.0;

            if (op.mode == Mode::LP12 || op.mode == Mode::LP24) {
                // LP: corner = first freq (scanning low-to-high) where mag drops below threshold.
                for (size_t i = 1; i < nPts; ++i) {
                    if (st.magDb[i] < threshold) {
                        // Linear interpolate between [i-1, i] in log-freq.
                        const double f0 = st.freqHz[i - 1], f1 = st.freqHz[i];
                        const double m0 = st.magDb[i - 1], m1 = st.magDb[i];
                        if (m1 < m0) {
                            const double t = (threshold - m0) / (m1 - m0);
                            cornerHz = f0 * std::pow(f1 / f0, t);
                        } else {
                            cornerHz = f0;
                        }
                        break;
                    }
                }
            } else if (op.mode == Mode::HP) {
                // HP: corner = last freq (scanning high-to-low) where mag drops below threshold.
                for (int i = (int) nPts - 2; i >= 0; --i) {
                    if (st.magDb[(size_t) i] < threshold) {
                        const double f0 = st.freqHz[(size_t) i];
                        const double f1 = st.freqHz[(size_t) i + 1];
                        const double m0 = st.magDb[(size_t) i];
                        const double m1 = st.magDb[(size_t) i + 1];
                        if (m1 > m0) {
                            const double t = (threshold - m0) / (m1 - m0);
                            cornerHz = f0 * std::pow(f1 / f0, t);
                        } else {
                            cornerHz = f1;
                        }
                        break;
                    }
                }
            } else {
                // BP / Notch: scan low-to-high for the first crossing above the passband ref.
                for (size_t i = 1; i < nPts; ++i) {
                    if (st.magDb[i] >= threshold) {
                        const double f0 = st.freqHz[i - 1], f1 = st.freqHz[i];
                        const double m0 = st.magDb[i - 1], m1 = st.magDb[i];
                        if (std::abs(m1 - m0) > 1.0e-12) {
                            const double t = (threshold - m0) / (m1 - m0);
                            cornerHz = f0 * std::pow(f1 / f0, t);
                        } else {
                            cornerHz = f0;
                        }
                        break;
                    }
                }
            }

            if (!std::isfinite(cornerHz) || cornerHz <= 0.0) {
                juce::Logger::writeToLog("[CharacterizationRunner] WARNING: corner_hz not found for "
                    + modelName + "/" + modeStr + " fc=" + juce::String(op.cutoffHz));
                cornerHz = -1.0;
            }
        }
    }

    // --- slope_db_oct: mag at 2*corner minus mag at corner (from stepped-sine) ---
    double slopeDbOct = -1.0;
    if (cornerHz > 0.0 && std::isfinite(cornerHz)) {
        const double magAtCorner     = interpMag(st.freqHz, st.magDb, cornerHz);
        const double magAtTwoCorner  = interpMag(st.freqHz, st.magDb, 2.0 * cornerHz);
        if (std::isfinite(magAtCorner) && std::isfinite(magAtTwoCorner)) {
            slopeDbOct = magAtTwoCorner - magAtCorner;
        } else {
            juce::Logger::writeToLog("[CharacterizationRunner] WARNING: slope_db_oct degenerate for "
                + modelName + "/" + modeStr + " fc=" + juce::String(op.cutoffHz));
        }
    }

    if (!std::isfinite(slopeDbOct))  slopeDbOct  = -1.0;
    if (!std::isfinite(methodDelta))  { /* methodDelta stays as-is */ }

    // --- Write CSV rows (one per probe freq per method) ---
    // Stepped rows: groupDelaySec is blank (written as empty string).
    // ESS rows: all fields populated.
    const size_t nFreqs = probeFreqs.size();
    for (size_t i = 0; i < nFreqs; ++i) {
        // Stepped row
        const double stMag   = (i < st.magDb.size())   ? st.magDb[i]   : 0.0;
        const double stPhase = (i < st.phaseRad.size()) ? st.phaseRad[i] : 0.0;
        csvRows += modelName + "," + modeStr + ","
                + juce::String(op.osFactor) + "," + osModeStr + ","
                + juce::String((int) op.hostSampleRate) + ","
                + juce::String(op.cutoffHz, 2) + ","
                + juce::String(op.resonance, 4) + ","
                + juce::String(op.drive, 4) + ","
                + juce::String(probeFreqs[i], 4) + ","
                + "stepped" + ","
                + juce::String(stMag, 6) + ","
                + juce::String(stPhase, 6) + ","
                + "\n";  // groupDelaySec blank for stepped

        // ESS row
        const double esMag   = (i < es.magDb.size())       ? es.magDb[i]       : 0.0;
        const double esPhase = (i < es.phaseRad.size())     ? es.phaseRad[i]     : 0.0;
        const double esGD    = (i < es.groupDelaySec.size())? es.groupDelaySec[i]: 0.0;
        csvRows += modelName + "," + modeStr + ","
                + juce::String(op.osFactor) + "," + osModeStr + ","
                + juce::String((int) op.hostSampleRate) + ","
                + juce::String(op.cutoffHz, 2) + ","
                + juce::String(op.resonance, 4) + ","
                + juce::String(op.drive, 4) + ","
                + juce::String(probeFreqs[i], 4) + ","
                + "ess" + ","
                + juce::String(esMag, 6) + ","
                + juce::String(esPhase, 6) + ","
                + juce::String(esGD, 9) + "\n";
    }

    // Absolute-level reductions (M3): peak = resonant peak gain; passband = gain at
    // the mode's passband anchor. st.magDb is absolute gain (output/input) in dB.
    const double peakGainDb = testdsp::Level::peakGainDb(st.magDb);
    const testdsp::Level::Passband anchor =
        (op.mode == Mode::HP) ? testdsp::Level::Passband::High
                              : testdsp::Level::Passband::Low;   // LP/LP12/BP/Notch -> Low (BP/Notch approximate)
    const double passbandGainDb = testdsp::Level::passbandGainDb(st.magDb, anchor);

    B1Result result;
    result.cornerHz      = cornerHz;
    result.slopeDbOct    = slopeDbOct;
    result.methodDeltaDb = methodDelta;
    result.peakGainDb     = peakGainDb;
    result.passbandGainDb = passbandGainDb;
    return result;
}

// ---------------------------------------------------------------------------
// runB2OnePoint — B2: resonance + self-oscillation at high resonance
//   op.resonance should already be the max resonance in the grid.
//   Kicks an impulse, captures a power-of-two window, finds FFT peak pitch.
//   selfoscCentsErr = 1200 * log2(measuredHz / cutoffHz).
//   Above 4 kHz: still recorded, logged as report-only.
//   Appends one row to csvRows.
// ---------------------------------------------------------------------------
CharacterizationRunner::B2Result CharacterizationRunner::runB2OnePoint(
        DeviceUnderTest& fut, const OperatingPoint& op, juce::String& csvRows) {

    const juce::String modelName = fut.name();
    const juce::String modeStr   = modeName(op.mode);
    const juce::String osModeStr = osModeName(op.osMode);

    B2Result result;

    // Power-of-two window size: 4096 gives ~23 Hz bin resolution at 96 kHz.
    // Must be power-of-two for Spectrum::magnitude.
    const int kCaptureLen = 4096;

    fut.setOperatingPoint(op);
    fut.reset();

    // Kick: one impulse followed by silence; capture the ring.
    auto impulse = testdsp::SignalGen::impulse(0.5f, 1);
    fut.process(impulse.data(), 1);

    std::vector<float> capture(static_cast<size_t>(kCaptureLen), 0.0f);
    // Warm: let the ring build up a bit before capturing (skip first 256 samples).
    std::vector<float> warmup(256, 0.0f);
    fut.process(warmup.data(), 256);

    // Capture kCaptureLen samples of the ring-down.
    fut.process(capture.data(), kCaptureLen);

    // FIX 3: energy guard — if the capture has no real energy (ring decayed or never
    // oscillated), store sentinel -1.0 for both fields rather than letting peakFreqHz
    // pick a noise bin and produce a bogus large cents value treated as valid.
    if (testdsp::Spectrum::maxAbs(capture) < 1.0e-4f) {
        juce::Logger::writeToLog("[CharacterizationRunner B2] WARNING: capture has no energy "
            "(ring decayed / never oscillated) for "
            + modelName + "/" + modeStr + " fc=" + juce::String(op.cutoffHz)
            + " res=" + juce::String(op.resonance, 3));
        result.selfoscHz       = -1.0;
        result.selfoscCentsErr = -1.0;
    } else {
        // FFT-peak pitch
        const double measuredHz = testdsp::Response::peakFreqHz(capture, op.hostSampleRate);

        if (!std::isfinite(measuredHz) || measuredHz <= 1.0) {
            juce::Logger::writeToLog("[CharacterizationRunner B2] WARNING: self-osc not detected for "
                + modelName + "/" + modeStr + " fc=" + juce::String(op.cutoffHz)
                + " res=" + juce::String(op.resonance, 3));
            result.selfoscHz       = -1.0;
            result.selfoscCentsErr = -1.0;
        } else {
            result.selfoscHz = measuredHz;
            result.selfoscCentsErr = 1200.0 * std::log2(measuredHz / op.cutoffHz);

            if (op.cutoffHz > 4000.0) {
                juce::Logger::writeToLog("[CharacterizationRunner B2] INFO: self-osc above 4 kHz "
                    "(report-only): fc=" + juce::String(op.cutoffHz)
                    + " measured=" + juce::String(measuredHz, 1) + " Hz"
                    + " err=" + juce::String(result.selfoscCentsErr, 1) + " cents");
            }
        }
    }

    // Append CSV row
    // Header: model,mode,osFactor,osMode,hostSR,cutoffHz,resonance,selfoscHz,selfosc_cents_err
    const double soHz  = std::isfinite(result.selfoscHz)       ? result.selfoscHz       : -1.0;
    const double soCe  = std::isfinite(result.selfoscCentsErr) ? result.selfoscCentsErr : -1.0;
    csvRows += modelName + "," + modeStr + ","
            + juce::String(op.osFactor) + "," + osModeStr + ","
            + juce::String((int) op.hostSampleRate) + ","
            + juce::String(op.cutoffHz, 2) + ","
            + juce::String(op.resonance, 4) + ","
            + juce::String(soHz, 4) + ","
            + juce::String(soCe, 4) + "\n";

    return result;
}

// ---------------------------------------------------------------------------
// runB3OnePoint — B3: distortion + aliasing at one operating point.
//   Called once per (mode, cutoff, osFactor) at the BASE operating point
//   (base resonance + base drive, osMode=Live, base hostSR). FIX 4.
//
//   THD measurement: Harmonics::thdDb at (cutoffHz, resonance, drive) from op.
//   The THD probe tone (probeHz) uses the grid operating point as-is.
//
//   Aliasing metric: inharmonic-energy per the design doc B3 (FIX 2).
//   Uses Spectrum::magnitude + Metrics::inharmonicDb on output from a FIXED
//   ISOLATION PROBE with a raised cutoff (~0.4*hostSR), drive=1.0, res=0.9,
//   tone at ~0.35*hostSR so H2 folds above base-rate Nyquist at os1 but NOT
//   at os2. This probe is INDEPENDENT of op.cutoffHz/drive/resonance — it
//   fingerprints the OS tier at a fixed condition so alias_db@os<N> is
//   comparable across OS factors.
//   Both probe conditions are written to separate distortion.csv columns (FIX 1).
//
//   Appends one row to csvRows.
// ---------------------------------------------------------------------------
CharacterizationRunner::B3Result CharacterizationRunner::runB3OnePoint(
        DeviceUnderTest& fut, const OperatingPoint& op, double probeHz,
        juce::String& csvRows) {

    const juce::String modelName = fut.name();
    const juce::String modeStr   = modeName(op.mode);
    const juce::String osModeStr = osModeName(op.osMode);

    B3Result result;

    // --- THD at the operating-point drive ---
    fut.setOperatingPoint(op);
    result.thdDb = testdsp::Harmonics::thdDb(fut, probeHz, op.hostSampleRate, 0.5f);
    if (!std::isfinite(result.thdDb)) {
        juce::Logger::writeToLog("[CharacterizationRunner B3] WARNING: thdDb not finite for "
            + modelName + "/" + modeStr + " fc=" + juce::String(op.cutoffHz));
        result.thdDb = -1.0;
    }

    // --- Aliasing isolation probe ---
    // Independent of the grid operating point. Fixed parameters chosen to maximise
    // foldback contrast between os1 and os2. Cutoff at 0.4*hostSR lets the probe
    // tone pass the LP filter; res=0.9 and drive=1.0 engage the nonlinear tanh path
    // so harmonics are generated. Tone at hostSR*0.35 → H2 = hostSR*0.70 which lies
    // above base-rate Nyquist (0.5*hostSR) at os1 (folds) but below internal Nyquist
    // (hostSR) at os2 (does not fold). These alias_* parameters are written to the
    // CSV explicitly so each column unambiguously labels its measurement source.
    const double aliasCutoffHz  = op.hostSampleRate * 0.4;
    const double aliasResonance = 0.9;
    const double aliasDrive     = 1.0;

    OperatingPoint aliasOp = op;
    aliasOp.drive     = aliasDrive;
    aliasOp.resonance = aliasResonance;
    aliasOp.cutoffHz  = aliasCutoffHz;
    fut.setOperatingPoint(aliasOp);
    fut.reset();

    // Use a power-of-two buffer so Spectrum::magnitude works correctly.
    const int kN = 1 << 14;  // 16384 samples
    const double aliasToneHz = op.hostSampleRate * 0.35;
    const int bin = std::max(2, (int) std::lround(aliasToneHz * kN / op.hostSampleRate));

    // Warm up
    std::vector<float> warm = testdsp::SignalGen::sine(0.5f, (double) bin * op.hostSampleRate / kN,
                                                        op.hostSampleRate, 8192);
    fut.process(warm.data(), (int) warm.size());

    // Capture bin-aligned window
    std::vector<float> cap = testdsp::SignalGen::binAlignedSine(0.5f, bin, kN);
    fut.process(cap.data(), kN);

    auto mag = testdsp::Spectrum::magnitude(cap);
    result.aliasDb = testdsp::Metrics::inharmonicDb(mag, bin);

    if (!std::isfinite(result.aliasDb)) {
        juce::Logger::writeToLog("[CharacterizationRunner B3] WARNING: aliasDb not finite for "
            + modelName + "/" + modeStr + " fc=" + juce::String(op.cutoffHz)
            + " os=" + juce::String(op.osFactor));
        result.aliasDb = -1.0;
    }

    // FIX 1: Honest CSV schema. The THD columns carry the grid operating point;
    // the alias_* columns carry the isolation probe's ACTUAL parameters.
    // Header: model,mode,osFactor,osMode,hostSR,cutoffHz,resonance,drive,
    //         thd_probeHz,thd_db,
    //         alias_cutoffHz,alias_resonance,alias_drive,alias_toneHz,alias_db
    const double tdb = std::isfinite(result.thdDb)   ? result.thdDb   : -1.0;
    const double adb = std::isfinite(result.aliasDb) ? result.aliasDb : -1.0;
    csvRows += modelName + "," + modeStr + ","
            + juce::String(op.osFactor) + "," + osModeStr + ","
            + juce::String((int) op.hostSampleRate) + ","
            + juce::String(op.cutoffHz, 2) + ","
            + juce::String(op.resonance, 4) + ","
            + juce::String(op.drive, 4) + ","
            + juce::String(probeHz, 4) + ","
            + juce::String(tdb, 6) + ","
            + juce::String(aliasCutoffHz, 2) + ","
            + juce::String(aliasResonance, 4) + ","
            + juce::String(aliasDrive, 4) + ","
            + juce::String(aliasToneHz, 4) + ","
            + juce::String(adb, 6) + "\n";

    return result;
}

// ---------------------------------------------------------------------------
// run — main entry point: B1 (dual-method magnitude + B4 phase/gd) + B2 (self-osc)
//       + B3 (distortion/aliasing). Writes response.csv, resonance.csv,
//       distortion.csv to outDir.
// ---------------------------------------------------------------------------
Summary CharacterizationRunner::run(DeviceUnderTest& fut, const Grid& g,
                                     const juce::File& outDir) {
    outDir.createDirectory();

    // CSV headers for all three batteries.
    juce::String b1CsvRows;
    b1CsvRows += "model,mode,osFactor,osMode,hostSR,cutoffHz,resonance,drive,"
                 "probeHz,method,magDb,phaseRad,groupDelaySec\n";

    juce::String b2CsvRows;
    b2CsvRows += "model,mode,osFactor,osMode,hostSR,cutoffHz,resonance,selfoscHz,selfosc_cents_err\n";

    // FIX 1: Honest distortion.csv schema. THD columns carry the grid operating point;
    // alias_* columns carry the isolation probe parameters (fixed raised cutoff, drive=1.0,
    // res=0.9, tone at ~0.35*hostSR). Column names unambiguously label their measurement source.
    juce::String b3CsvRows;
    b3CsvRows += "model,mode,osFactor,osMode,hostSR,cutoffHz,resonance,drive,"
                 "thd_probeHz,thd_db,"
                 "alias_cutoffHz,alias_resonance,alias_drive,alias_toneHz,alias_db\n";

    Summary summary;

    // Determine the "base" resonance for summary key uniqueness.
    // Base case: osFactor=1, OsMode::Live, lowest resonance in grid, hostRate=96000
    // (or first in grid if 96000 not present).
    const double baseRes = g.resonances.empty() ? 0.0 :
                           *std::min_element(g.resonances.begin(), g.resonances.end());
    const double baseDrive = g.drives.empty() ? 0.0 :
                             *std::min_element(g.drives.begin(), g.drives.end());
    const double baseHost = [&]() {
        auto it = std::find_if(g.hostRates.begin(), g.hostRates.end(),
                               [](double r) { return std::abs(r - 96000.0) < 0.5; });
        return (it != g.hostRates.end()) ? 96000.0 : (g.hostRates.empty() ? 96000.0 : g.hostRates[0]);
    }();

    // B2: run once per (mode, cutoff) at max resonance, osFactor=1, Live, baseHost.
    // We need the max resonance from the grid.
    const double maxRes = g.resonances.empty() ? 0.0 :
                          *std::max_element(g.resonances.begin(), g.resonances.end());

    // B3: use a mid probe freq for THD (aim for 1 kHz, or the nearest grid probe >= 500 Hz).
    // For aliasing tone: fixed 4 kHz (clipped inside runB3OnePoint to 3-5 kHz).
    const double b3MidHz = [&]() {
        if (g.probeFreqs.empty()) return 1000.0;
        // Find probe freq closest to 1 kHz.
        double best = g.probeFreqs[0];
        double bestDiff = std::abs(best - 1000.0);
        for (double f : g.probeFreqs) {
            const double diff = std::abs(f - 1000.0);
            if (diff < bestDiff) { bestDiff = diff; best = f; }
        }
        return best;
    }();

    for (Mode mode : g.modes) {
        // supports() is non-const and mutates mode — call OUTSIDE any measurement loop.
        if (!fut.supports(mode))
            continue;

        for (double cutoff : g.cutoffs) {
            // Build the (stable) summary key prefix for this (model, mode, cutoff) triple.
            const juce::String keyBase = fut.name() + "/" + modeName(mode)
                                       + "/fc" + juce::String((int) std::round(cutoff));

            // --- B2: one measurement per (mode, cutoff) at max resonance ---
            // Run at os=1, Live, baseHost so B2 summary keys are deterministic.
            {
                OperatingPoint opB2;
                opB2.mode           = mode;
                opB2.cutoffHz       = cutoff;
                opB2.resonance      = maxRes;
                opB2.drive          = baseDrive;
                opB2.osFactor       = 1;
                opB2.osMode         = OsMode::Live;
                opB2.hostSampleRate = baseHost;
                auto b2r = runB2OnePoint(fut, opB2, b2CsvRows);

                const double soHz = std::isfinite(b2r.selfoscHz)       ? b2r.selfoscHz       : -1.0;
                const double soCe = std::isfinite(b2r.selfoscCentsErr) ? b2r.selfoscCentsErr : -1.0;
                summary[keyBase + "/selfosc_cents_err"] = soCe;
                (void) soHz;  // selfosc_cents_err is the headline; Hz is in CSV
            }

            for (double resonance : g.resonances) {
                for (double drive : g.drives) {
                    for (int osFactor : g.osFactors) {
                        for (OsMode osMode : g.osModes) {
                            for (double hostRate : g.hostRates) {
                                OperatingPoint op;
                                op.mode           = mode;
                                op.cutoffHz       = cutoff;
                                op.resonance      = resonance;
                                op.drive          = drive;
                                op.osFactor       = osFactor;
                                op.osMode         = osMode;
                                op.hostSampleRate = hostRate;

                                auto b1r = runB1OnePoint(fut, op, g.probeFreqs, b1CsvRows);

                                // Only store B1 summary metrics for the base operating point
                                // to keep keys unique and deterministic.
                                // NOTE for downstream consumers (Tasks 9-12): corner_hz is the
                                // measured physical -3 dB point, which for an authentic 4-pole
                                // Moog ladder at res=0 sits near 0.44*fc (NOT the nominal
                                // cutoff); slope_db_oct is read one octave above that corner, a
                                // transition-band slope rather than the asymptotic stopband rate.
                                // (Grid values are exact literals; tolerance compares avoid
                                // -Wfloat-equal without changing selection.)
                                const bool isBase = (osFactor == 1)
                                                 && (osMode == OsMode::Live)
                                                 && (std::abs(resonance - baseRes) < 1.0e-9)
                                                 && (std::abs(hostRate - baseHost) < 0.5);
                                if (isBase) {
                                    const double c = std::isfinite(b1r.cornerHz)     ? b1r.cornerHz     : -1.0;
                                    const double s = std::isfinite(b1r.slopeDbOct)   ? b1r.slopeDbOct   : -1.0;
                                    const double d = std::isfinite(b1r.methodDeltaDb)? b1r.methodDeltaDb: -1.0;
                                    summary[keyBase + "/corner_hz"]      = c;
                                    summary[keyBase + "/slope_db_oct"]   = s;
                                    summary[keyBase + "/method_delta_db"]= d;
                                }

                                // M3: level metrics are stored at MAX resonance (where the
                                // resonant peak and the authentic passband droop actually appear),
                                // at the same os=1/Live/baseHost base point for deterministic keys.
                                const bool isLevelBase = (osFactor == 1)
                                                      && (osMode == OsMode::Live)
                                                      && (std::abs(resonance - maxRes) < 1.0e-9)
                                                      && (std::abs(hostRate - baseHost) < 0.5);
                                if (isLevelBase) {
                                    const double pk = std::isfinite(b1r.peakGainDb)     ? b1r.peakGainDb     : -300.0;
                                    const double pb = std::isfinite(b1r.passbandGainDb) ? b1r.passbandGainDb : -300.0;
                                    summary[keyBase + "/peak_gain_db"]     = pk;
                                    summary[keyBase + "/passband_gain_db"] = pb;
                                }
                            }
                        }
                    }
                }
            }

            // --- FIX 4: B3 once per (mode, cutoff, osFactor) at base conditions ---
            // Base: baseResonance, baseDrive, osMode=Live, baseHost. This avoids
            // redundant work from running B3 for every res/drive/osMode/host combo
            // and eliminates confusing duplicate distortion.csv rows.
            // alias_db@os<N> summary keys are written for every osFactor.
            // thd_db is written at osFactor=1 only (single headline per cutoff).
            for (int osFactor : g.osFactors) {
                OperatingPoint opB3;
                opB3.mode           = mode;
                opB3.cutoffHz       = cutoff;
                opB3.resonance      = baseRes;
                opB3.drive          = baseDrive;
                opB3.osFactor       = osFactor;
                opB3.osMode         = OsMode::Live;
                opB3.hostSampleRate = baseHost;
                auto b3r = runB3OnePoint(fut, opB3, b3MidHz, b3CsvRows);

                const double adb = std::isfinite(b3r.aliasDb) ? b3r.aliasDb : -1.0;
                summary[keyBase + "/alias_db@os" + juce::String(osFactor)] = adb;

                if (osFactor == 1) {
                    const double tdb = std::isfinite(b3r.thdDb) ? b3r.thdDb : -1.0;
                    summary[keyBase + "/thd_db"] = tdb;
                }
            }
        }
    }

    // Write all three CSVs.
    outDir.getChildFile("response.csv").replaceWithText(b1CsvRows);
    outDir.getChildFile("resonance.csv").replaceWithText(b2CsvRows);
    outDir.getChildFile("distortion.csv").replaceWithText(b3CsvRows);

    return summary;
}

} // namespace chz
