#include "RunLog.h"
#include <cmath>

#ifdef _WIN32
 #include <process.h>
#else
 #include <unistd.h>
#endif

namespace runlog {

namespace {
int currentPid() {
#ifdef _WIN32
    return _getpid();
#else
    return (int) getpid();
#endif
}
} // namespace

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

static juce::String detectRunner() {
    const char* br = std::getenv("BERNIE_RUNNER");
    const char* ga = std::getenv("GITHUB_ACTIONS");
    const char* cc = std::getenv("CLAUDECODE");

    if (br != nullptr && br[0] != '\0') return juce::String(br);
    if (ga != nullptr) return "ci";
    if (cc != nullptr) return "claude";
    return "terminal";
}

static juce::File defaultDir() {
    if (const char* d = std::getenv("BERNIE_RUNLOG_DIR"))
        return juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(d));
    return juce::File::getCurrentWorkingDirectory().getChildFile(".franklin/runs");
}

int lastSuiteTestCount() {
    // Honor the "zero disk side effects" contract: read nothing when logging is off.
    if (const char* n = std::getenv("BERNIE_NO_RUNLOG"); n != nullptr && juce::String(n) == "1") return -1;
    auto dir = defaultDir();
    if (!dir.isDirectory()) return -1;
    juce::Array<juce::File> files;
    dir.findChildFiles(files, juce::File::findFiles, false, "*-suite-*.ndjson");
    juce::File newest;
    juce::int64 newestMs = std::numeric_limits<juce::int64>::min();
    for (const auto& f : files) {
        const auto m = f.getLastModificationTime().toMilliseconds();
        if (m > newestMs) { newestMs = m; newest = f; }
    }
    if (newest == juce::File()) return -1;
    juce::StringArray lines;
    lines.addLines(newest.loadFileAsString());
    for (int i = lines.size() - 1; i >= 0; --i) {   // last non-empty line = the end event
        if (lines[i].trim().isEmpty()) continue;
        auto parsed = juce::JSON::parse(lines[i]);
        if (parsed.isObject() && parsed.getProperty("ev", "").toString() == "end")
            return (int) parsed.getProperty("tests", -1);
        return -1;   // newest run has no end yet (running/crashed) — don't estimate from it
    }
    return -1;
}

Writer::Writer(const juce::String& kind) : Writer(kind, defaultDir(), 1000, 3600 * 1000) {}

Writer::Writer(const juce::String& kind, const juce::File& dir, int64_t throttleMs, int64_t slowAfterMs)
    : kind_(kind), throttleMs_(throttleMs), slowAfterMs_(slowAfterMs) {
    if (std::getenv("BERNIE_NO_RUNLOG") != nullptr && juce::String(std::getenv("BERNIE_NO_RUNLOG")) == "1") { enabled_ = false; return; }
    dir.createDirectory();
    t0Ms_ = juce::Time::currentTimeMillis();
    auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    file_ = dir.getChildFile(stamp + "-" + kind + "-" + juce::String(currentPid()) + ".ndjson");
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
      << ",\"kind\":\"" << jsonEscape(kind_)
      << "\",\"argv\":[" << a << "],\"pid\":" << currentPid()
      << ",\"buildType\":\"" << bt << "\",\"runner\":\"" << jsonEscape(detectRunner()) << "\"";
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
