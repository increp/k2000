#pragma once
#include <juce_core/juce_core.h>

namespace util {

// THE single conversion from a UTF-8 C-string to juce::String.
//
// juce::String(const char*) assumes the bytes are ASCII and mangles anything
// above 0x7F: a literal like "Filter \xE2\x86\x92 Shaper" (U+2192 →) becomes
// "Filter â<box><box> Shaper". Every non-ASCII string literal in this codebase
// MUST come through u8() so it is decoded as UTF-8 once, correctly, everywhere.
//
// Plain ASCII literals may pass through u8() too (it is a harmless no-op for
// them), so it is safe to use as the default wrapper for any C-string headed
// for the UI / parameter layer.
inline juce::String u8(const char* utf8) {
    return juce::String(juce::CharPointer_UTF8(utf8));
}

} // namespace util
