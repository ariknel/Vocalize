#pragma once
#include <JuceHeader.h>

namespace MicInput::Colours
{
    constexpr auto BG      = juce::Colour(0xff0F1117);  // main background
    constexpr auto CARD    = juce::Colour(0xff1A1D27);  // card surface
    constexpr auto BORDER  = juce::Colour(0xff2A2D3A);  // card borders
    constexpr auto ACCENT  = juce::Colour(0xff6366F1);  // indigo accent
    constexpr auto GREEN   = juce::Colour(0xff10B981);
    constexpr auto AMBER   = juce::Colour(0xffF59E0B);
    constexpr auto RED     = juce::Colour(0xffEF4444);
    constexpr auto TEXT    = juce::Colour(0xffE2E8F0);
    constexpr auto DIM     = juce::Colour(0xff64748B);
    constexpr auto M_LOW   = juce::Colour(0xff10B981);
    constexpr auto M_MID   = juce::Colour(0xffF59E0B);
    constexpr auto M_PEAK  = juce::Colour(0xffEF4444);

    inline juce::Colour forLatency(float ms)
    {
        if (ms < 15.f) return GREEN;
        if (ms < 25.f) return AMBER;
        return RED;
    }

    inline const char* qualityLabel(float ms)
    {
        if (ms < 10.f)  return "Imperceptible";
        if (ms < 15.f)  return "Excellent";
        if (ms < 20.f)  return "Very Good";
        if (ms < 30.f)  return "Acceptable";
        if (ms < 50.f)  return "Noticeable";
        return "High";
    }
}
