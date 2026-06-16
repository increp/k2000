#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// A titled, bordered Summit module. Children are placed within contentBounds().
// reserved == true draws the frame dimmed and inert (a future-phase placeholder
// that holds its layout slot so the panel never reflows when the phase lands).
// spine == true tints the border with the constant-Summit-spine accent.
class Section : public juce::Component {
public:
    Section(const juce::String& title, bool spine = false, bool reserved = false);

    void setReserved(bool reserved);
    bool isReserved() const { return reserved_; }

    // Area inside the border + title strip, for the owner to lay out children.
    juce::Rectangle<int> contentBounds() const;

    void paint(juce::Graphics&) override;

private:
    static constexpr int titleH_ = 18;
    juce::String title_;
    bool spine_;
    bool reserved_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Section)
};
