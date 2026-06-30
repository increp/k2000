#include "CharacterizationRunner.h"
#include "../testdsp/SteppedSine.h"
#include "../testdsp/EssResponse.h"
#include "../testdsp/MethodAgreement.h"
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
        FilterUnderTest& fut, const OperatingPoint& op,
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

    // --- method delta ---
    const double methodDelta = testdsp::MethodAgreement::maxMagDeltaDb(st.magDb, es.magDb);

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

    B1Result result;
    result.cornerHz      = cornerHz;
    result.slopeDbOct    = slopeDbOct;
    result.methodDeltaDb = methodDelta;
    return result;
}

// ---------------------------------------------------------------------------
// run — main entry point for 8a (B1 + B4)
// B2/B3 seam: add runB2OnePoint / runB3OnePoint helpers and call them here.
// ---------------------------------------------------------------------------
Summary CharacterizationRunner::run(FilterUnderTest& fut, const Grid& g,
                                     const juce::File& outDir) {
    outDir.createDirectory();

    // CSV header
    juce::String csvRows;
    csvRows += "model,mode,osFactor,osMode,hostSR,cutoffHz,resonance,drive,"
               "probeHz,method,magDb,phaseRad,groupDelaySec\n";

    Summary summary;

    // Determine the "base" resonance for summary key uniqueness.
    // Base case: osFactor=1, OsMode::Live, lowest resonance in grid, hostRate=96000
    // (or first in grid if 96000 not present).
    const double baseRes = g.resonances.empty() ? 0.0 :
                           *std::min_element(g.resonances.begin(), g.resonances.end());
    const double baseHost = [&]() {
        auto it = std::find_if(g.hostRates.begin(), g.hostRates.end(),
                               [](double r) { return std::abs(r - 96000.0) < 0.5; });
        return (it != g.hostRates.end()) ? 96000.0 : (g.hostRates.empty() ? 96000.0 : g.hostRates[0]);
    }();

    for (Mode mode : g.modes) {
        // supports() is non-const and mutates mode — call OUTSIDE any measurement loop.
        if (!fut.supports(mode))
            continue;

        for (double cutoff : g.cutoffs) {
            // Build the (stable) summary key prefix for this (model, mode, cutoff) triple.
            const juce::String keyBase = fut.name() + "/" + modeName(mode)
                                       + "/fc" + juce::String((int) std::round(cutoff));

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

                                auto result = runB1OnePoint(fut, op, g.probeFreqs, csvRows);

                                // Only store summary metrics for the base operating point
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
                                    const double c = std::isfinite(result.cornerHz)     ? result.cornerHz     : -1.0;
                                    const double s = std::isfinite(result.slopeDbOct)   ? result.slopeDbOct   : -1.0;
                                    const double d = std::isfinite(result.methodDeltaDb)? result.methodDeltaDb: -1.0;
                                    summary[keyBase + "/corner_hz"]      = c;
                                    summary[keyBase + "/slope_db_oct"]   = s;
                                    summary[keyBase + "/method_delta_db"]= d;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Write response.csv
    const auto csvFile = outDir.getChildFile("response.csv");
    csvFile.replaceWithText(csvRows);

    return summary;
}

} // namespace chz
