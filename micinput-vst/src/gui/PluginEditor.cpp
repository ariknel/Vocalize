#include "gui/PluginEditor.h"
#include "plugin/PluginProcessor.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
MicInputEditor::MicInputEditor(MicInputProcessor& p)
    : AudioProcessorEditor(&p)
    , m_processor(p)
{
    setSize(420, 390);
    setResizable(false, false);
    setLookAndFeel(&m_laf);

    // Device combo
    m_deviceCombo.setTextWhenNothingSelected("System Default");
    m_deviceCombo.addListener(this);
    addAndMakeVisible(m_deviceCombo);

    // Refresh button
    m_refreshBtn.onClick = [this] { onRefresh(); };
    addAndMakeVisible(m_refreshBtn);

    // Stat cards
    addAndMakeVisible(m_captureCard);
    addAndMakeVisible(m_blockCard);
    addAndMakeVisible(m_outputCard);
    addAndMakeVisible(m_totalCard);

    // Level meter
    addAndMakeVisible(m_meter);

    // Mode selector
    m_modeSelector.onModeChanged = [this](int mode) {
        m_processor.setMode(mode);
    };
    addAndMakeVisible(m_modeSelector);

    // Populate device list
    populateDeviceCombo();

    // Restore selected device in combo
    int idx = 0;
    {
        auto devices = m_processor.getAvailableDevices();
        // Find by iterating (device index stored in processor)
        // For now just select first item (default)
    }
    m_deviceCombo.setSelectedId(1, juce::dontSendNotification);

    startTimerHz(30);
}

MicInputEditor::~MicInputEditor()
{
    stopTimer();
    m_deviceCombo.removeListener(this);
    setLookAndFeel(nullptr);  // REQUIRED — prevents crash on editor destroy
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────
void MicInputEditor::resized()
{
    using namespace juce;
    auto area = getLocalBounds();

    // Header (44px)
    area.removeFromTop(44);

    // Device row (52px)
    auto deviceRow = area.removeFromTop(52).reduced(8, 6);
    deviceRow.removeFromTop(16); // label space
    auto comboRow = deviceRow;
    m_refreshBtn.setBounds(comboRow.removeFromRight(28).reduced(0, 2));
    comboRow.removeFromRight(4);
    m_deviceCombo.setBounds(comboRow);

    area.removeFromTop(4);

    // Stat cards row 1 (70px)
    auto row1 = area.removeFromTop(70).reduced(8, 0);
    int cardW = (row1.getWidth() - 6) / 2;
    m_captureCard.setBounds(row1.removeFromLeft(cardW));
    row1.removeFromLeft(6);
    m_blockCard.setBounds(row1);

    area.removeFromTop(4);

    // Stat cards row 2 (70px)
    auto row2 = area.removeFromTop(70).reduced(8, 0);
    m_outputCard.setBounds(row2.removeFromLeft(cardW));
    row2.removeFromLeft(6);
    m_totalCard.setBounds(row2);

    area.removeFromTop(4);

    // Level meter (38px)
    m_meter.setBounds(area.removeFromTop(38).reduced(8, 0));

    area.removeFromTop(4);

    // Mode selector (fills remaining space minus status bar)
    area.removeFromBottom(22); // status bar
    m_modeSelector.setBounds(area.reduced(8, 0));
}

// ─────────────────────────────────────────────────────────────────────────────
// Paint
// ─────────────────────────────────────────────────────────────────────────────
void MicInputEditor::paint(juce::Graphics& g)
{
    g.fillAll(MicInput::Colours::BG);
    drawHeader(g);
    drawStatusBar(g);

    // "INPUT DEVICE" label
    g.setColour(MicInput::Colours::DIM);
    g.setFont(juce::Font(juce::FontOptions(10.0f)).withExtraKerningFactor(0.05f));
    g.drawText("INPUT DEVICE", 8, 50, getWidth() - 16, 14,
                juce::Justification::centredLeft);
}

void MicInputEditor::drawHeader(juce::Graphics& g)
{
    using namespace MicInput::Colours;

    // Background strip
    g.setColour(CARD);
    g.fillRect(0, 0, getWidth(), 44);
    g.setColour(BORDER);
    g.drawLine(0, 43.5f, (float)getWidth(), 43.5f, 1.0f);

    // Pulsing dot (green if capturing, red if not)
    const bool cap = m_capturing;
    g.setColour(cap ? GREEN : RED);
    g.fillEllipse(12.0f, 16.0f, 10.0f, 10.0f);

    // Title
    g.setColour(TEXT);
    g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    g.drawText("MicInput", 30, 10, 140, 24, juce::Justification::centredLeft);

    // Version
    g.setColour(DIM);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("v0.1", getWidth() - 40, 10, 32, 24, juce::Justification::centred);

    // Thread opt summary (tiny, dimmed, far right)
    juce::String optSummary = m_processor.getThreadOptSummary();
    if (optSummary.isNotEmpty())
    {
        g.setFont(juce::Font(juce::FontOptions(9.0f)));
        g.drawText(optSummary, getWidth() - 200, 32, 190, 10,
                    juce::Justification::centredRight);
    }
}

void MicInputEditor::drawStatusBar(juce::Graphics& g)
{
    using namespace MicInput::Colours;
    const int barY = getHeight() - 22;

    g.setColour(CARD);
    g.fillRect(0, barY, getWidth(), 22);
    g.setColour(BORDER);
    g.drawLine(0, (float)barY + 0.5f, (float)getWidth(), (float)barY + 0.5f, 1.0f);

    g.setFont(juce::Font(juce::FontOptions(11.0f)));

    if (!m_capturing)
    {
        g.setColour(RED);
        std::string errStr = m_processor.getCaptureError();
        juce::String err = errStr.empty()
            ? "Not capturing - check Windows mic privacy settings"
            : juce::String(errStr.c_str());
        g.drawText(err, 8, barY + 3, getWidth() - 16, 16,
                    juce::Justification::centredLeft);
        return;
    }

    uint64_t underruns = m_processor.underruns.load();
    if (underruns > m_lastUnderruns)
    {
        g.setColour(AMBER);
        g.drawText("CAPTURING  |  Underruns: " + juce::String((int64_t)underruns),
                    8, barY + 3, getWidth() - 16, 16,
                    juce::Justification::centredLeft);
    }
    else
    {
        g.setColour(GREEN);
        g.drawText("CAPTURING  |  ARM TRACK TO RECORD",
                    8, barY + 3, getWidth() - 16, 16,
                    juce::Justification::centredLeft);
    }
    m_lastUnderruns = underruns;
}

// ─────────────────────────────────────────────────────────────────────────────
// 30Hz Timer — reads all atomics, updates all components
// ─────────────────────────────────────────────────────────────────────────────
void MicInputEditor::timerCallback()
{
    auto& p = m_processor;

    m_capturing = p.isCapturing.load();

    // ── Stat cards ────────────────────────────────────────────────────────────
    // CAPTURE card
    const float capMs   = p.captureMs.load();
    const bool  iac3    = p.usedIAC3.load();
    const bool  excl    = p.isExclusive.load();
    juce::String capSub = excl ? "Exclusive" : (iac3 ? "IAudioClient3" : "Standard shared");
    m_captureCard.setMsWithSub(capMs, capSub);

    // BLOCK card
    const float blkMs = p.blockMs.load();
    m_blockCard.setMsAndSamples(blkMs, getAudioProcessor()->getBlockSize());

    // OUTPUT card
    m_outputCard.setOutputType(
        p.outputIsUsb.load(),
        p.outputIsBluetooth.load(),
        juce::String(p.currentOutputProfile.deviceName.c_str()));

    // TOTAL card
    m_totalCard.setLatencyTotal(p.totalLatMs.load());

    // ── Level meter ───────────────────────────────────────────────────────────
    m_meter.setLevels(p.levelL.load(), p.levelR.load());

    // ── Mode selector ─────────────────────────────────────────────────────────
    m_modeSelector.setDeviceProfile(
        p.currentDeviceProfile,
        p.currentOutputProfile,
        blkMs);

    repaint();
}

// ─────────────────────────────────────────────────────────────────────────────
// Device combo
// ─────────────────────────────────────────────────────────────────────────────
void MicInputEditor::populateDeviceCombo()
{
    m_deviceCombo.clear(juce::dontSendNotification);
    m_deviceCombo.addItem("System Default", 1);

    auto devices = m_processor.getAvailableDevices();
    for (int i = 0; i < (int)devices.size(); ++i)
        m_deviceCombo.addItem(juce::String(devices[i].name.c_str()), i + 2);
}

void MicInputEditor::comboBoxChanged(juce::ComboBox* box)
{
    if (box != &m_deviceCombo) return;
    int id  = m_deviceCombo.getSelectedId();
    int idx = (id <= 1) ? -1 : id - 2;
    m_processor.selectDevice(idx);
}

void MicInputEditor::onRefresh()
{
    m_processor.refreshDevices();
    int prevId = m_deviceCombo.getSelectedId();
    populateDeviceCombo();
    m_deviceCombo.setSelectedId(prevId, juce::dontSendNotification);
}
