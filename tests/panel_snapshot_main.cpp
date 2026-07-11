// Offscreen panel snapshot — renders the real K2000AudioProcessorEditor to a PNG
// without a window or screenshot permissions. Local dev tool for GUI tuning loops
// (this box's GNOME portal refuses programmatic capture); NOT in CTest/CI.
// Usage: k2000_panel_snapshot [out.png] [width height]
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/PluginProcessor.h"
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    K2000AudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    if (editor == nullptr) {
        std::cerr << "createEditor() returned null (built with K2000_TESTING?)\n";
        return 1;
    }

    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(
        argc > 1 ? argv[1] : "panel.png");
    if (argc > 3)
        editor->setSize(juce::String(argv[2]).getIntValue(),
                        juce::String(argv[3]).getIntValue());

    const float scale = argc > 4 ? juce::String(argv[4]).getFloatValue() : 1.0f;
    auto img = editor->createComponentSnapshot(editor->getLocalBounds(), true,
                                               scale > 0.0f ? scale : 1.0f);
    out.deleteFile();
    juce::FileOutputStream os(out);
    if (!os.openedOk()) { std::cerr << "cannot write " << out.getFullPathName() << "\n"; return 1; }
    juce::PNGImageFormat png;
    if (!png.writeImageToStream(img, os)) { std::cerr << "png encode failed\n"; return 1; }
    std::cout << "wrote " << out.getFullPathName() << "  ("
              << img.getWidth() << "x" << img.getHeight() << ")\n";
    return 0;
}
