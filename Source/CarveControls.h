#pragma once

// Pill toggles bound to bool parameters, and the Collision / Classic switch.

#include "CarveUI.h"

namespace carveui
{
//==============================================================================
// Pill toggle bound to a bool parameter.
class PillToggle final : public juce::Button
{
public:
    PillToggle(const juce::String& offText, juce::Colour onColour, juce::RangedAudioParameter& parameter,
               const juce::String& onText = {})
        : juce::Button(offText), offLabel(offText), onLabel(onText.isEmpty() ? offText : onText), colour(onColour),
          attachment(parameter, [this](float v) { setToggleState(v > 0.5f, juce::dontSendNotification); repaint(); })
    {
        setClickingTogglesState(false);
        onClick = [this] { attachment.setValueAsCompleteGesture(getToggleState() ? 0.0f : 1.0f); };
        attachment.sendInitialUpdate();
    }

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        paintPill(g, getLocalBounds().toFloat(), getToggleState(), over, colour, getToggleState() ? onLabel : offLabel);
    }

    static void paintPill(juce::Graphics& g, juce::Rectangle<float> area, bool on, bool over, juce::Colour colour,
                          const juce::String& label)
    {
        area = area.reduced(0.5f);
        const auto radius = area.getHeight() * 0.5f;
        g.setColour(on ? colour.withAlpha(0.16f) : palette::plot);
        g.fillRoundedRectangle(area, radius);
        g.setColour(on ? colour : (over ? palette::dim : palette::edge));
        g.drawRoundedRectangle(area, radius, 1.0f);
        g.setColour(on ? colour : palette::muted);
        g.fillEllipse(area.getX() + 11.0f, area.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour(on ? palette::text : palette::muted);
        g.setFont(font(14.0f, Weight::semibold));
        g.drawText(label, area.withTrimmedLeft(22.0f).withTrimmedRight(8.0f), juce::Justification::centred);
    }

private:
    juce::String offLabel, onLabel;
    juce::Colour colour;
    juce::ParameterAttachment attachment;
};

//==============================================================================
class ModeSwitch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ModeSwitch(juce::RangedAudioParameter& parameter)
        : attachment(parameter, [this](float v) { selected = v > 0.5f ? 1 : 0; repaint(); })
    {
        setTitle("Mode");
        setTooltip("COLLISION measures both tracks and cuts only where they actually overlap.\n"
                   "CLASSIC cuts wherever the sidechain is loud.");
        attachment.sendInitialUpdate();
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(palette::plot);
        g.fillRoundedRectangle(area, 7.0f);
        g.setColour(palette::edge);
        g.drawRoundedRectangle(area, 7.0f, 1.0f);

        const std::array<const char*, 2> names { "COLLISION", "CLASSIC" };
        for (int index = 0; index < 2; ++index)
        {
            const auto cell = segment(index);
            if (index == selected)
            {
                g.setColour(index == 0 ? palette::collision.withAlpha(0.18f) : palette::edge);
                g.fillRoundedRectangle(cell.reduced(3.0f), 5.0f);
                g.setColour(index == 0 ? palette::collision : palette::muted);
                g.drawRoundedRectangle(cell.reduced(3.0f), 5.0f, 1.0f);
            }
            g.setColour(index == selected ? palette::text : palette::muted);
            g.setFont(font(15.0f, Weight::bold));
            g.drawText(names[static_cast<std::size_t>(index)], cell, juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto index = segment(0).contains(event.position) ? 0 : 1;
        if (index != selected)
            attachment.setValueAsCompleteGesture(static_cast<float>(index));
    }

private:
    juce::Rectangle<float> segment(int index) const
    {
        auto area = getLocalBounds().toFloat();
        return index == 0 ? area.removeFromLeft(area.getWidth() * 0.55f) : area.withTrimmedLeft(area.getWidth() * 0.55f);
    }

    juce::ParameterAttachment attachment;
    int selected = 0;
};
}
