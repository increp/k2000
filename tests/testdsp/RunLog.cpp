#include "RunLog.h"
#include <unistd.h>
#include <cmath>

namespace runlog {

juce::String jsonEscape(const juce::String& s) {
    juce::String out;
    for (auto t = s.getCharPointer(); !t.isEmpty(); ++t) {
        auto c = *t;
        switch (c) {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n";  break;
            case '\r': out << "\\r";  break;
            case '\t': out << "\\t";  break;
            default:
                if (c < 0x20) out << "\\u" + juce::String::toHexString((int) c).paddedLeft('0', 4);
                else          out << juce::String::charToString(c);
        }
    }
    return out;
}

static juce::File defaultDir() {
    if (const char* d = std::getenv("BERNIE_RUNLOG_DIR"))
        return juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(d));
    return juce::File::getCurrentWorkingDirectory().getChildFile(".franklin/runs");
}

Writer::Writer(const juce::String& kind) : Writer(kind, defaultDir(), 1000, 3600 * 1000) {}

Writer::Writer(const juce::String& kind, const juce::File& dir, int64_t throttleMs, int64_t slowAfterMs)
    : throttleMs_(throttleMs), slowAfterMs_(slowAfterMs) {
    if (std::getenv("BERNIE_NO_RUNLOG") != nullptr && juce::String(std::getenv("BERNIE_NO_RUNLOG")) == "1") { enabled_ = false; return; }
    dir.createDirectory();
    t0Ms_ = juce::Time::currentTimeMillis();
    auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    file_ = dir.getChildFile(stamp + "-" + kind + "-" + juce::String((int) getpid()) + ".ndjson");
    if (!file_.create().wasOk()) enabled_ = false;
}

bool Writer::enabled() const { return enabled_; }
juce::File Writer::file() const { return file_; }

void Writer::line(const juce::String& jsonObj) {
    if (!enabled_) return;
    if (!file_.appendText(jsonObj + "\n", false, false, "\n")) enabled_ = false;   // best-effort
}

void Writer::start(const juce::StringArray& argv, const juce::String& model,
                   const juce::String& grid, int total) {
    if (!enabled_) return;
    juce::String a; for (const auto& s : argv) a << (a.isEmpty() ? "" : ",") << "\"" << jsonEscape(s) << "\"";
#ifdef NDEBUG
    const char* bt = "Release";
#else
    const char* bt = "Debug";
#endif
    juce::String j;
    j << "{\"ev\":\"start\",\"ts\":" << juce::Time::currentTimeMillis()
      << ",\"kind\":\"" << jsonEscape(file_.getFileName().contains("-suite-") ? "suite" : "chz")
      << "\",\"argv\":[" << a << "],\"pid\":" << (int) getpid()
      << ",\"buildType\":\"" << bt << "\"";
    if (const char* sha = std::getenv("BERNIE_GIT_SHA")) j << ",\"gitSha\":\"" << jsonEscape(sha) << "\"";
    if (model.isNotEmpty()) j << ",\"model\":\"" << jsonEscape(model) << "\"";
    if (grid.isNotEmpty())  j << ",\"grid\":\""  << jsonEscape(grid)  << "\"";
    if (total >= 0)         j << ",\"total\":"   << total;
    j << "}";
    line(j);
}

void Writer::progress(int done, int total, const juce::String& label) {
    if (!enabled_) return;
    const auto now = juce::Time::currentTimeMillis();
    const auto gap = (now - t0Ms_ > slowAfterMs_) ? throttleMs_ * 10 : throttleMs_;
    if (done != total && now - lastMs_ < gap) return;
    lastMs_ = now;
    juce::String j;
    j << "{\"ev\":\"progress\",\"ts\":" << now << ",\"done\":" << done
      << ",\"total\":" << total << ",\"label\":\"" << jsonEscape(label) << "\"}";
    line(j);
}

void Writer::test(const juce::String& name, const juce::String& sub, int passes,
                  int failures, const juce::StringArray& messages) {
    if (!enabled_) return;
    juce::String m; for (const auto& s : messages) m << (m.isEmpty() ? "" : ",") << "\"" << jsonEscape(s) << "\"";
    juce::String j;
    j << "{\"ev\":\"test\",\"ts\":" << juce::Time::currentTimeMillis()
      << ",\"name\":\"" << jsonEscape(name) << "\",\"sub\":\"" << jsonEscape(sub)
      << "\",\"ok\":" << (failures == 0 ? "true" : "false")
      << ",\"passes\":" << passes << ",\"failures\":" << failures
      << ",\"messages\":[" << m << "]}";
    line(j);
}

void Writer::end(const juce::String& outcome, double durationS,
                 const std::vector<Check>& checks, int tests, int failed) {
    if (!enabled_) return;
    juce::String c;
    for (const auto& ch : checks) {
        c << (c.isEmpty() ? "" : ",") << "{\"name\":\"" << jsonEscape(ch.name)
          << "\",\"measured\":" << juce::String(ch.measured, 6);
        if (!std::isnan(ch.expected)) c << ",\"expected\":" << juce::String(ch.expected, 6);
        c << ",\"verdict\":\"" << jsonEscape(ch.verdict) << "\"}";
    }
    juce::String j;
    j << "{\"ev\":\"end\",\"ts\":" << juce::Time::currentTimeMillis()
      << ",\"outcome\":\"" << jsonEscape(outcome) << "\",\"durationS\":" << juce::String(durationS, 3);
    if (tests >= 0)  j << ",\"tests\":" << tests;
    if (failed >= 0) j << ",\"failed\":" << failed;
    j << ",\"checks\":[" << c << "]}";
    line(j);
}

} // namespace runlog
