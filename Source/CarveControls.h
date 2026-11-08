#pragma once

// Pill toggles bound to bool parameters, and the Collision / Classic switch.

#include "CarveUI.h"
#include "MeterHub.h"

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

//==============================================================================
// Sidechain source switch that also says, live, whether the routed sidechain is arriving.
class SidechainButton final : public juce::Button
{
public:
    SidechainButton(juce::RangedAudioParameter& parameter, MeterHub& h)
        : juce::Button("SIDECHAIN"), hub(h),
          attachment(parameter, [this](float v) { setToggleState(v > 0.5f, juce::dontSendNotification); repaint(); })
    {
        setClickingTogglesState(false);
        onClick = [this] { attachment.setValueAsCompleteGesture(getToggleState() ? 0.0f : 1.0f); };
        attachment.sendInitialUpdate();
    }

    enum class State { internal, waiting, notRouted, silent, live };
    State state() const
    {
        if (!getToggleState()) return State::internal;
        if (!hub.hasFrame) return State::waiting;
        if (hub.frame.source == carve::TriggerSource::unavailable) return State::notRouted;
        if (hub.frame.triggerInputPeak < 0.0001f) return State::silent;
        return State::live;
    }

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const auto s = state();
        const auto colour = s == State::live ? palette::key
                          : s == State::notRouted ? palette::collision
                          : s == State::silent ? palette::track : palette::muted;
        const char* label = s == State::internal ? "SIDECHAIN: INTERNAL"
                          : s == State::waiting ? "SIDECHAIN: EXTERNAL"
                          : s == State::notRouted ? "SIDECHAIN: NOT ROUTED"
                          : s == State::silent ? "SIDECHAIN: SILENT" : "SIDECHAIN: LIVE";
        PillToggle::paintPill(g, getLocalBounds().toFloat(), s != State::internal, over, colour, label);
    }

private:
    MeterHub& hub;
    juce::ParameterAttachment attachment;
};

//==============================================================================
// "How do I connect the sidechain?" - step by step for the common DAWs.
class SidechainHelp final : public juce::Component
{
public:
    std::function<void()> onClose;

    SidechainHelp()
    {
        close.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible(close);
        for (int i = 0; i < 5; ++i)
        {
            auto* b = tabs.add(new juce::TextButton(names()[i]));
            b->setClickingTogglesState(false);
            b->onClick = [this, i] { page = i; updateTabs(); repaint(); };
            addAndMakeVisible(b);
        }
        updateTabs();
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour(palette::panel);
        g.fillRoundedRectangle(area, 12.0f);
        g.setColour(palette::key.withAlpha(0.6f));
        g.drawRoundedRectangle(area.reduced(0.5f), 12.0f, 1.0f);
        g.setColour(palette::text);
        g.setFont(font(19.0f, Weight::bold));
        g.drawText("CONNECT THE SIDECHAIN", 20, 14, 400, 24, juce::Justification::centredLeft);
        g.setColour(palette::muted);
        g.setFont(font(14.5f));
        g.drawText("Carve listens to a second track (vocal, 808, lead...) and cuts the beat only where they collide.",
                   20, 40, getWidth() - 40, 20, juce::Justification::centredLeft);

        auto text = getLocalBounds().reduced(20, 0).withTrimmedTop(108).withTrimmedBottom(16);
        const auto steps = stepsFor(page);
        g.setFont(font(15.5f));
        int n = 1;
        for (const auto& line : steps)
        {
            auto row = text.removeFromTop(juce::jmax(24, juce::roundToInt(24.0f * std::ceil(juce::GlyphArrangement::getStringWidth(font(15.5f), line) / (float) (text.getWidth() - 34)))));
            g.setColour(palette::key);
            g.fillEllipse(static_cast<float>(row.getX()), static_cast<float>(row.getY()) + 3.0f, 20.0f, 20.0f);
            g.setColour(palette::background);
            g.setFont(font(13.0f, Weight::bold));
            g.drawText(juce::String(n++), row.getX(), row.getY() + 3, 20, 20, juce::Justification::centred);
            g.setColour(palette::text);
            g.setFont(font(15.5f));
            g.drawFittedText(line, row.withTrimmedLeft(32), juce::Justification::topLeft, 3, 1.0f);
            text.removeFromTop(4);
        }
        g.setColour(palette::muted);
        g.setFont(font(13.5f));
        g.drawFittedText("The pill under the face turns blue (SIDECHAIN: LIVE) the moment the routed audio arrives. "
                         "NOT ROUTED means the host is not sending the sidechain bus; SILENT means it is connected but the source track is quiet.",
                         getLocalBounds().reduced(20, 0).removeFromBottom(44), juce::Justification::centredLeft, 2);
    }

    void resized() override
    {
        close.setBounds(getWidth() - 20 - 32, 14, 32, 30);
        int x = 20;
        for (auto* b : tabs)
        {
            const auto w = juce::roundToInt(juce::GlyphArrangement::getStringWidth(font(14.0f, Weight::semibold), b->getButtonText())) + 28;
            b->setBounds(x, 68, w, 28);
            x += w + 6;
        }
    }

private:
    static const juce::StringArray& names()
    {
        static const juce::StringArray n { "FL Studio", "Ableton Live", "Logic Pro", "Studio One", "Reaper / other" };
        return n;
    }
    static juce::StringArray stepsFor(int page)
    {
        switch (page)
        {
            case 0: return {
                "Put Carve on the mixer track of the beat (the track you want to carve).",
                "Select the source's mixer track (vocal, 808...), then right-click the small arrow at the bottom of Carve's track and choose \"Sidechain to this track\".",
                "In Carve's window, click the cog at the top-left and open Processing > Connections. Keep the beat on the main input and assign the source track to Sidechain (its small light must be on).",
                "Set Carve to SIDECHAIN: EXTERNAL. The pill turns blue when the audio arrives." };
            case 1: return {
                "Put Carve on the beat's track.",
                "In Carve's device title bar, click the triangle to show the sidechain section (or open the plugin's I/O).",
                "Turn Sidechain on and pick the source track (vocal, 808...) in Audio From; choose Post FX.",
                "Set Carve to SIDECHAIN: EXTERNAL." };
            case 2: return {
                "Insert Carve on the beat's channel strip.",
                "In the plugin window's header, open the Side Chain menu and choose the source track or bus.",
                "Set Carve to SIDECHAIN: EXTERNAL." };
            case 3: return {
                "Insert Carve on the beat's channel.",
                "Click the sidechain button in Carve's plugin header to activate the sidechain input.",
                "On the source channel, add a Send to Carve's sidechain input.",
                "Set Carve to SIDECHAIN: EXTERNAL." };
            default: return {
                "Reaper: give the beat's track 4 channels, then send the source track to channels 3/4 of the beat's track.",
                "Open Carve's pin connector and make sure inputs 3/4 feed Carve's sidechain inputs.",
                "Other hosts: route the source to Carve's \"Sidechain\" input bus (it has two channels).",
                "Set Carve to SIDECHAIN: EXTERNAL." };
        }
    }
    void updateTabs()
    {
        for (int i = 0; i < tabs.size(); ++i)
        {
            tabs[i]->setColour(juce::TextButton::buttonColourId, i == page ? palette::key.withAlpha(0.2f) : palette::plot);
            tabs[i]->setColour(juce::TextButton::textColourOffId, i == page ? palette::text : palette::muted);
        }
    }

    juce::TextButton close { juce::CharPointer_UTF8("\xe2\x9c\x95") };
    juce::OwnedArray<juce::TextButton> tabs;
    int page = 0;
};
}
