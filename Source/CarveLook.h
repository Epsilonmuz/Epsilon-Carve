#pragma once

// LookAndFeel: machined knobs, combo boxes, buttons, embedded font.

#include "CarveUI.h"
#include "../Shared/epsilon_ui/EpsilonSignature.h"

namespace carveui
{
//==============================================================================
class CarveLook final : public juce::LookAndFeel_V4
{
public:
    CarveLook()
    {
        setColour(juce::ComboBox::backgroundColourId, palette::plot);
        setColour(juce::ComboBox::outlineColourId, palette::edge);
        setColour(juce::ComboBox::textColourId, palette::text);
        setColour(juce::ComboBox::arrowColourId, palette::muted);
        setColour(juce::PopupMenu::backgroundColourId, palette::panel);
        setColour(juce::PopupMenu::textColourId, palette::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, palette::edge);
        setColour(juce::PopupMenu::highlightedTextColourId, palette::text);
        setColour(juce::Slider::textBoxTextColourId, palette::text);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, palette::edge);
        setColour(juce::TextEditor::backgroundColourId, palette::plot);
        setColour(juce::TextEditor::textColourId, palette::text);
        setColour(juce::TextEditor::focusedOutlineColourId, palette::edge);
        setColour(juce::CaretComponent::caretColourId, palette::text);
        setColour(juce::TooltipWindow::backgroundColourId, palette::panel);
        setColour(juce::TooltipWindow::textColourId, palette::text);
        setColour(juce::TooltipWindow::outlineColourId, palette::edge);
        setColour(juce::TextButton::buttonColourId, palette::plot);
        setColour(juce::TextButton::textColourOffId, palette::muted);
    }

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& f) override
    {
        return font(f.getHeight(), f.isBold() ? Weight::bold : Weight::medium).getTypefacePtr();
    }

    // Epsilon Sounds signature coin-edge knob (static layers cached)
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float value, float, float, juce::Slider& slider) override
    {
        epsilon_ui::signature::knob(g, juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height).reduced(4.0f),
                                    value, slider, epsilon_ui::Tier::Epsilon);
    }

    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* label = LookAndFeel_V4::createSliderTextBox(slider);
        label->setFont(font(static_cast<bool>(slider.getProperties()["hero"]) ? 20.0f : 15.0f, Weight::semibold));
        label->setJustificationType(juce::Justification::centred);
        return label;
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                      juce::ComboBox& box) override
    {
        const auto area = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        g.setColour(palette::plot);
        g.fillRoundedRectangle(area, 6.0f);
        g.setColour(box.isMouseOver(true) ? palette::dim : palette::edge);
        g.drawRoundedRectangle(area.reduced(0.5f), 6.0f, 1.0f);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(box.getLocalBounds().reduced(8, 1));
        label.setFont(font(17.0f, Weight::semibold));
        label.setJustificationType(juce::Justification::centred);
    }

    juce::Font getPopupMenuFont() override { return font(16.0f); }

    // Epsilon Sounds signature keycap; toggles carry the champagne lume bar when on
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down) override
    {
        epsilon_ui::signature::keycap(g, button, over, down, epsilon_ui::Tier::Epsilon);
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        epsilon_ui::signature::keycapText(g, b, getTextButtonFont(b, b.getHeight()));
    }
};
}
