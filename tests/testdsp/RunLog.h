#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <limits>
#include <cstdint>

namespace runlog {

juce::String jsonEscape(const juce::String& s);

class Writer {
public:
    explicit Writer(const juce::String& kind);
    bool enabled() const;
    void start(const juce::StringArray& argv, const juce::String& model = {},
               const juce::String& grid = {}, int total = -1);
    void progress(int done, int total, const juce::String& label);
    void test(const juce::String& name, const juce::String& sub, int passes,
              int failures, const juce::StringArray& messages);
    struct Check {
        juce::String name;
        double measured;
        double expected = std::numeric_limits<double>::quiet_NaN();
        juce::String verdict;
    };
    void end(const juce::String& outcome, double durationS,
             const std::vector<Check>& checks = {}, int tests = -1, int failed = -1);

    // test seams:
    Writer(const juce::String& kind, const juce::File& dir, int64_t throttleMs, int64_t slowAfterMs);
    juce::File file() const;

private:
    void line(const juce::String& jsonObj);
    juce::File file_;
    juce::String kind_;
    bool enabled_ = true;
    int64_t t0Ms_ = 0, lastMs_ = 0, throttleMs_ = 1000, slowAfterMs_ = 3600 * 1000;
};

} // namespace runlog
