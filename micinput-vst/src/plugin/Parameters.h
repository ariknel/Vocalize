#pragma once
#include <JuceHeader.h>
#include <string_view>

namespace MicInput::Params
{
    // ── Parameter IDs — single source of truth ────────────────────────────
    constexpr std::string_view MODE = "mode";
    // 0 = Shared Auto (IAC3 → IAC1 fallback)
    // 1 = Exclusive

    // ── Parameter layout for APVTS ────────────────────────────────────────
    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add(std::make_unique<AudioParameterInt>(
            ParameterID{Params::MODE.data(), 1},
            "Capture Mode",
            0, 1, 0));

        return layout;
    }

} // namespace MicInput::Params
