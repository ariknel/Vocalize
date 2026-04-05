#include "gui/LookAndFeel.h"

MicInputLookAndFeel::MicInputLookAndFeel()
{
    using namespace MicInput::Colours;
    using namespace juce;

    setColour(ComboBox::backgroundColourId,   CARD);
    setColour(ComboBox::textColourId,         TEXT);
    setColour(ComboBox::outlineColourId,      BORDER);
    setColour(ComboBox::arrowColourId,        DIM);
    setColour(ComboBox::buttonColourId,       CARD);
    setColour(PopupMenu::backgroundColourId,  CARD);
    setColour(PopupMenu::textColourId,        TEXT);
    setColour(PopupMenu::highlightedBackgroundColourId, ACCENT);
    setColour(PopupMenu::highlightedTextColourId,       TEXT);
    setColour(TextButton::buttonColourId,     CARD);
    setColour(TextButton::buttonOnColourId,   ACCENT);
    setColour(TextButton::textColourOnId,     TEXT);
    setColour(TextButton::textColourOffId,    DIM);
    setColour(Label::textColourId,            TEXT);
    setColour(Label::backgroundColourId,      Colour(0x00000000));
}

void MicInputLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h,
                                        bool, int, int, int, int,
                                        juce::ComboBox& box)
{
    using namespace MicInput::Colours;
    auto bounds = juce::Rectangle<float>(0, 0, (float)w, (float)h);
    g.setColour(CARD);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(box.hasKeyboardFocus(false) ? ACCENT : BORDER);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    // Arrow
    const float arrowH = h * 0.4f;
    juce::Path arrow;
    arrow.addTriangle(w - 20.f, h * 0.5f - arrowH * 0.25f,
                      w - 12.f, h * 0.5f - arrowH * 0.25f,
                      w - 16.f, h * 0.5f + arrowH * 0.25f);
    g.setColour(DIM);
    g.fillPath(arrow);
}

void MicInputLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                                juce::Button& btn,
                                                const juce::Colour&,
                                                bool highlighted, bool down)
{
    using namespace MicInput::Colours;
    auto b = btn.getLocalBounds().toFloat();
    juce::Colour fill = btn.getToggleState() ? ACCENT : CARD;
    if (highlighted || down) fill = fill.brighter(0.1f);
    g.setColour(fill);
    g.fillRoundedRectangle(b, 4.0f);
    g.setColour(btn.getToggleState() ? ACCENT : BORDER);
    g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);
}
