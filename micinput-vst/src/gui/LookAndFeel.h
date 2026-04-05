#pragma once
#include <JuceHeader.h>
#include "gui/Colours.h"

class MicInputLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MicInputLookAndFeel();

    void drawComboBox(juce::Graphics&, int w, int h, bool isDown,
                      int, int, int, int, juce::ComboBox&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& bg,
                              bool highlighted, bool down) override;

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(juce::FontOptions(13.0f));
    }
    juce::Font getLabelFont(juce::Label&) override
    {
        return juce::Font(juce::FontOptions(12.0f));
    }
};
