#pragma once

// LookAndFeel: machined knobs, combo boxes, buttons, embedded font.

#include "CarveUI.h"

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

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float value, float start, float end, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                   static_cast<float>(width), static_cast<float>(height));
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 4.0f;
        if (radius <= 4.0f)
            return;

        const auto centre = bounds.getCentre();
        const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const auto hero = static_cast<bool>(slider.getProperties()["hero"]);
        const auto angle = start + value * (end - start);
        const auto ring = radius - 2.0f;
        const auto thickness = hero ? 5.0f : 3.0f;

        // Tick ring
        for (int tick = 0; tick <= 20; ++tick)
        {
            const auto tickAngle = start + (end - start) * static_cast<float>(tick) / 20.0f;
            const auto inner = centre.getPointOnCircumference(ring + thickness * 0.5f + 2.0f, tickAngle);
            const auto outer = centre.getPointOnCircumference(ring + thickness * 0.5f + (tick % 5 == 0 ? 6.0f : 4.0f), tickAngle);
            g.setColour(palette::edge.brighter(tick % 5 == 0 ? 0.35f : 0.0f));
            g.drawLine(juce::Line<float>(inner, outer), 1.0f);
        }

        juce::Path rail, active;
        rail.addCentredArc(centre.x, centre.y, ring, ring, 0.0f, start, end, true);
        active.addCentredArc(centre.x, centre.y, ring, ring, 0.0f, start, angle, true);
        const juce::PathStrokeType stroke(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour(palette::edge);
        g.strokePath(rail, stroke);
        if (hero)
        {
            g.setColour(accent.withAlpha(0.18f));
            g.strokePath(active, juce::PathStrokeType(thickness + 9.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }
        g.setColour(accent);
        g.strokePath(active, stroke);

        // Body: machined puck with a light catch across the top
        const auto bodyRadius = ring - thickness - (hero ? 8.0f : 5.0f);
        const auto body = juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre);
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillEllipse(body.translated(0.0f, 3.0f).expanded(1.5f));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2c3441), body.getX(), body.getY(),
                                               juce::Colour(0xff10141a), body.getRight(), body.getBottom(), false));
        g.fillEllipse(body);
        const auto inset = body.reduced(bodyRadius * 0.16f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1a1f28), inset.getX(), inset.getBottom(),
                                               juce::Colour(0xff252c37), inset.getX(), inset.getY(), false));
        g.fillEllipse(inset);
        juce::Path catchLight;
        catchLight.addCentredArc(centre.x, centre.y, bodyRadius - 1.0f, bodyRadius - 1.0f, 0.0f, -2.3f, 0.7f, true);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.strokePath(catchLight, juce::PathStrokeType(1.4f));
        g.setColour(palette::edge.brighter(0.3f));
        g.drawEllipse(body, 1.0f);

        const auto inner = centre.getPointOnCircumference(bodyRadius * 0.34f, angle);
        const auto outer = centre.getPointOnCircumference(bodyRadius * 0.86f, angle);
        g.setColour(hero ? accent : palette::text);
        g.drawLine(juce::Line<float>(inner, outer), hero ? 3.2f : 2.2f);
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

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down) override
    {
        const auto area = button.getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(down ? palette::edge : palette::plot);
        g.fillRoundedRectangle(area, 6.0f);
        g.setColour(over ? palette::dim : palette::edge);
        g.drawRoundedRectangle(area, 6.0f, 1.0f);
    }
};
}
