#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <cmath>
#include <cstdlib>

namespace testdsp {

// Tiny CSV golden store: one `key,value` line per metric (decision 2026-06-20 —
// CSVs, not WAV vectors). std::map iterates in sorted-key order, so saves are
// deterministic and git-diffable.
struct GoldenIO {
    static std::map<juce::String, double> load(const juce::File& f) {
        std::map<juce::String, double> m;
        if (! f.existsAsFile()) return m;
        juce::StringArray lines;
        f.readLines(lines);
        for (const auto& line : lines) {
            const auto t = line.trim();
            if (t.isEmpty() || t.startsWithChar('#')) continue;
            const int comma = t.lastIndexOfChar(',');
            if (comma < 0) continue;
            m[t.substring(0, comma).trim()] = t.substring(comma + 1).trim().getDoubleValue();
        }
        return m;
    }
    static void save(const juce::File& f, const std::map<juce::String, double>& m) {
        juce::String out;
        for (const auto& kv : m) out << kv.first << "," << juce::String(kv.second, 6) << "\n";
        f.getParentDirectory().createDirectory();
        f.replaceWithText(out);
    }
};

// Update-or-assert helper for a fixture. With env BERNIE_UPDATE_GOLDEN set, it
// collects measured values and writes the CSV on flush() (the intentional
// voicing-update workflow); otherwise it asserts each value within tol of the
// loaded golden, so an accidental regression fails CI and a deliberate change is
// a reviewable CSV diff. Golden dir is the compile-time BERNIE_GOLDEN_DIR.
struct GoldenSet {
    juce::File file;
    bool updating;
    std::map<juce::String, double> data;

    explicit GoldenSet(const juce::String& name)
        : file(juce::File(BERNIE_GOLDEN_DIR).getChildFile(name + ".csv")),
          updating(std::getenv("BERNIE_UPDATE_GOLDEN") != nullptr) {
        if (! updating) data = GoldenIO::load(file);
    }
    template <class UT>
    void check(UT& t, const juce::String& key, double measured, double tol) {
        if (updating) { data[key] = measured; return; }
        const auto it = data.find(key);
        t.expect(it != data.end(),
                 "golden missing key '" + key + "' in " + file.getFileName()
                 + " (run with BERNIE_UPDATE_GOLDEN=1 to create)");
        if (it == data.end()) return;
        t.expect(std::abs(measured - it->second) <= tol,
                 key + ": measured " + juce::String(measured, 3)
                 + " vs golden " + juce::String(it->second, 3)
                 + " (tol " + juce::String(tol, 3) + ")");
    }
    void flush() { if (updating) GoldenIO::save(file, data); }
};

} // namespace testdsp
