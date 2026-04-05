#pragma once
#include <JuceHeader.h>
#include "gui/Colours.h"

class LevelMeter : public juce::Component
{
public:
    void setLevels(float rmsL, float rmsR)
    {
        m_l = rmsL;
        m_r = rmsR;

        // Peak hold: hold for ~60 frames (2 seconds at 30Hz), then decay
        if (rmsL >= m_peakL) { m_peakL = rmsL; m_peakHoldL = 60; }
        else if (m_peakHoldL > 0) --m_peakHoldL;
        else m_peakL = std::max(0.0f, m_peakL - 0.01f);

        if (rmsR >= m_peakR) { m_peakR = rmsR; m_peakHoldR = 60; }
        else if (m_peakHoldR > 0) --m_peakHoldR;
        else m_peakR = std::max(0.0f, m_peakR - 0.01f);
    }

    void paint(juce::Graphics& g) override
    {
        using namespace MicInput::Colours;
        const int pad  = 2;
        const int labelW = 14;
        const int dbW    = 40;
        const int meterX = labelW + pad;
        const int meterW = getWidth() - meterX - dbW - pad;
        const int rowH   = (getHeight() - pad) / 2;

        drawRow(g, "L", m_l, m_peakL, 0,       meterX, meterW, rowH);
        drawRow(g, "R", m_r, m_peakR, rowH + pad, meterX, meterW, rowH);
    }

private:
    void drawRow(juce::Graphics& g, const char* label,
                 float rms, float peak,
                 int y, int meterX, int meterW, int h)
    {
        using namespace MicInput::Colours;
        const int labelW = 14;
        const int dbW    = 40;

        // Label
        g.setColour(DIM);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText(label, 0, y, labelW, h, juce::Justification::centred);

        // Track
        g.setColour(juce::Colour(0xff1e2235));
        g.fillRoundedRectangle((float)meterX, (float)y,
                                (float)meterW, (float)h, 3.0f);

        // Level bar gradient
        if (rms > 0.001f)
        {
            float fillW = rms * meterW;
            juce::ColourGradient grad(M_LOW, (float)meterX, 0,
                                      M_PEAK, (float)(meterX + meterW), 0, false);
            grad.addColour(0.6, M_MID);
            g.setGradientFill(grad);
            g.fillRoundedRectangle((float)meterX, (float)y,
                                    fillW, (float)h, 3.0f);
        }

        // Peak marker
        if (peak > 0.01f)
        {
            float px = meterX + peak * meterW;
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.fillRect((int)px, y, 2, h);
        }

        // dB label
        float db = rms > 0.0001f ? 20.0f * std::log10(rms) : -90.0f;
        juce::String dbStr = db < -60.0f ? "-inf" :
                             (juce::String(db, 1) + "dB");
        g.setColour(DIM);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(dbStr,
                   meterX + meterW + 4, y,
                   36, h, juce::Justification::centredLeft);
    }

    float m_l = 0.f, m_r = 0.f;
    float m_peakL = 0.f, m_peakR = 0.f;
    int   m_peakHoldL = 0, m_peakHoldR = 0;
};
