#pragma once
#include <JuceHeader.h>
#include "gui/LookAndFeel.h"
#include "gui/Colours.h"
#include "gui/components/StatCard.h"
#include "gui/components/LevelMeter.h"
#include "gui/components/ModeSelector.h"

class MicInputProcessor;

class MicInputEditor : public juce::AudioProcessorEditor,
                       private juce::Timer,
                       private juce::ComboBox::Listener
{
public:
    explicit MicInputEditor(MicInputProcessor& processor);
    ~MicInputEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void comboBoxChanged(juce::ComboBox*) override;
    void populateDeviceCombo();
    void onRefresh();
    void drawHeader(juce::Graphics&);
    void drawStatusBar(juce::Graphics&);

    MicInputProcessor& m_processor;
    MicInputLookAndFeel m_laf;

    // Device row
    juce::ComboBox  m_deviceCombo;
    juce::TextButton m_refreshBtn{"R"};

    // Stat cards
    StatCard m_captureCard{"CAPTURE"};
    StatCard m_blockCard  {"BLOCK SIZE"};
    StatCard m_outputCard {"OUTPUT"};
    StatCard m_totalCard  {"TOTAL RTL"};

    // Level meter
    LevelMeter m_meter;

    // Mode selector
    ModeSelector m_modeSelector;

    // State for status bar
    juce::String m_statusText;
    bool         m_capturing = false;
    uint64_t     m_lastUnderruns = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MicInputEditor)
};
