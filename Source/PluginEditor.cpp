#include "PluginEditor.h"
#include "CarveLook.h"
#include "MeterHub.h"
#include "UnmaskFace.h"
#include "CarveControls.h"
#include "CollisionDisplay.h"

using namespace carveui;

//==============================================================================
class EpsilonCarveAudioProcessorEditor::Panel final : public juce::Component
{
public:
    Panel(EpsilonCarveAudioProcessor& p, CarveLook& look)
        : processor(p),
          hub(p),
          display(p, hub),
          face(hub, p.params, look),
          mode(*p.params.getParameter("mode")),
          guard("WIDTH GUARD", palette::key, *p.params.getParameter("guard")),
          delta("DELTA", palette::collision, *p.params.getParameter("delta"), "DELTA IS ON"),
          sidechain("SIDECHAIN: INTERNAL", palette::key, *p.params.getParameter("external"), "SIDECHAIN: EXTERNAL")
    {
        setOpaque(true);
        hub.onUpdate = [this]
        {
            display.repaint();
            face.repaint();
        };
        for (auto* c : std::initializer_list<juce::Component*> { &display, &face, &mode, &guard, &delta, &sidechain, &advanced, &abSwitch })
            addAndMakeVisible(c);
        guard.setTooltip("Carve only the centre (Mid) of the track. The stereo sides keep their width.");
        delta.setTooltip("Hear only what Carve removes. Turn it off before you bounce.");
        sidechain.setTooltip("EXTERNAL: the routed sidechain (your vocal) drives the cuts.\n"
                             "INTERNAL: the track carves itself, like a 32-band dynamic EQ.");
        advanced.setTooltip("Show or hide Sensitivity, Attack, Release, Mix, Width Guard and Delta.");
        abSwitch.onSelect = [this](int slot) { processor.selectSlot(slot); syncPreset(); };
        abSwitch.onCopy = [this] { processor.copyActiveSlotToOther(); };
        abSwitch.setTooltip("A/B: two complete settings to compare. Click A or B to switch; the arrow copies the active one to the other.");
        advanced.onClick = [this]
        {
            processor.params.state.setProperty("advancedView", !showAdvanced(), nullptr);
            applyView();
        };

        struct Spec { const char* id; const char* name; double initial; bool hero; juce::Colour colour; const char* tip; };
        const std::array<Spec, 7> specs { {
            { "sensitivity", "SENSITIVITY", 0.0, false, palette::muted, "How strongly the sidechain counts. Raise it for quiet or distant sources." },
            { "attack", "ATTACK", 15.0, false, palette::muted, "How fast the cut arrives when the sidechain comes in." },
            { "low", "LOW CUT", carve::lowestBandHz, false, palette::text.withAlpha(0.8f), "Lowest frequency Carve may cut. Raise it to protect kick and bass." },
            { "amount", "AMOUNT", 25.0, true, palette::collision, "How hard Carve cuts. Start around 25-40%." },
            { "high", "HIGH CUT", carve::highestBandHz, false, palette::text.withAlpha(0.8f), "Highest frequency Carve may cut. Lower it to keep cymbals and air." },
            { "release", "RELEASE", 180.0, false, palette::muted, "How fast the track recovers after the sidechain stops." },
            { "mix", "MIX", 100.0, false, palette::muted, "Blend of carved and original signal." },
        } };

        for (std::size_t index = 0; index < specs.size(); ++index)
        {
            const auto& spec = specs[index];
            auto& knob = knobs[index];
            knob.setLookAndFeel(&look);
            knob.getProperties().set("hero", spec.hero);
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, spec.hero ? 110 : 90, spec.hero ? 26 : 22);
            knob.setDoubleClickReturnValue(true, spec.initial);
            knob.setColour(juce::Slider::rotarySliderFillColourId, spec.colour);
            knob.setTitle(spec.name);
            knob.setTooltip(spec.tip);
            addAndMakeVisible(knob);
            attachments[index] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params, spec.id, knob);

            auto& label = labels[index];
            label.setText(spec.name, juce::dontSendNotification);
            label.setFont(font(spec.hero ? 18.0f : 14.0f, Weight::bold));
            label.setJustificationType(juce::Justification::centred);
            label.setColour(juce::Label::textColourId, spec.hero ? palette::text : palette::muted);
            label.setInterceptsMouseClicks(false, false);
            addAndMakeVisible(label);
        }

        presetBox.setLookAndFeel(&look);
        for (int index = 0; index < processor.getNumPrograms(); ++index)
            presetBox.addItem(processor.getProgramName(index), index + 1);
        presetBox.setSelectedId(processor.getCurrentProgram() + 1, juce::dontSendNotification);
        presetBox.onChange = [this] { processor.setCurrentProgram(presetBox.getSelectedId() - 1); };
        presetBox.setTitle("Preset");
        addAndMakeVisible(presetBox);
        for (auto* button : { &previous, &next })
        {
            button->setLookAndFeel(&look);
            addAndMakeVisible(*button);
        }
        previous.onClick = [this] { step(-1); };
        next.onClick = [this] { step(1); };
        applyView();
    }

    ~Panel() override
    {
        hub.onUpdate = nullptr;
        for (auto& knob : knobs)
            knob.setLookAndFeel(nullptr);
        presetBox.setLookAndFeel(nullptr);
        previous.setLookAndFeel(nullptr);
        next.setLookAndFeel(nullptr);
    }

    void syncPreset()
    {
        // The preset name gets an asterisk once any control has moved since it was loaded.
        const auto id = processor.getCurrentProgram() + 1;
        const auto modified = processor.isPresetModified();
        const auto wanted = processor.getProgramName(id - 1) + (modified ? " *" : "");
        if (presetBox.getText() != wanted || (!modified && presetBox.getSelectedId() != id))
        {
            if (modified)
                presetBox.setText(wanted, juce::dontSendNotification);
            else
                presetBox.setSelectedId(id, juce::dontSendNotification);
        }
        abSwitch.setActive(processor.getActiveSlot());
        // Delta stays visible (even in Simple view) whenever it is on, so it can't be forgotten.
        const auto deltaVisible = showAdvanced() || plainValue(processor.params, "delta") > 0.5f;
        if (delta.isVisible() != deltaVisible)
            applyView();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(palette::background);

        // Chiselled corner: the Carve signature
        juce::Path notch;
        notch.startNewSubPath(0.0f, 0.0f);
        notch.lineTo(170.0f, 0.0f);
        notch.lineTo(124.0f, 66.0f);
        notch.lineTo(0.0f, 66.0f);
        notch.closeSubPath();
        g.setGradientFill(juce::ColourGradient(palette::collision.withAlpha(0.22f), 0.0f, 0.0f,
                                               palette::collision.withAlpha(0.0f), 170.0f, 66.0f, false));
        g.fillPath(notch);

        g.setColour(palette::text);
        g.setFont(font(14.0f, Weight::bold));
        g.drawText(juce::CharPointer_UTF8("E\xce\xa3  EPSILON SOUNDS"), 24, 10, 160, 18, juce::Justification::centredLeft);
        g.setFont(font(40.0f, Weight::bold));
        g.drawText("CARVE", 22, 24, 130, 40, juce::Justification::centredLeft);
        g.setColour(palette::collision);
        g.fillRect(126.0f, 52.0f, 20.0f, 5.0f);
        g.setColour(palette::muted);
        g.setFont(font(14.0f, Weight::semibold));
        g.drawText("COLLISION-AWARE SIDECHAIN EQ", 156, 40, 210, 18, juce::Justification::centredLeft);
        g.setColour(palette::dim);
        g.drawText("v0.5.2", 156, 22, 60, 18, juce::Justification::centredLeft);

        g.setColour(palette::edge);
        g.drawHorizontalLine(controlsTop - 6, 22.0f, static_cast<float>(getWidth()) - 22.0f);
    }

    void resized() override
    {
        const auto width = getWidth();
        presetBox.setBounds(430, 18, 230, 34);
        previous.setBounds(presetBox.getX() - 38, 18, 34, 34);
        next.setBounds(presetBox.getRight() + 4, 18, 34, 34);
        mode.setBounds(width - 22 - 220, 18, 220, 34);

        face.setBounds(22, 66, 270, 318);
        display.setBounds(302, 66, width - 302 - 22, 318);

        const auto toggleY = 392;
        sidechain.setBounds(22, toggleY, 200, 30);
        abSwitch.setBounds(236, toggleY, 132, 30);
        advanced.setBounds(width / 2 - 70, toggleY, 140, 30);
        guard.setBounds(width - 22 - 150 - 10 - 150, toggleY, 150, 30);
        delta.setBounds(width - 22 - 150, toggleY, 150, 30);
        layoutKnobs();
    }

private:
    // A | B | copy. Paints like the mode switch.
    class ABSwitch final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        std::function<void(int)> onSelect;
        std::function<void()> onCopy;
        void setActive(int slot) { if (slot != active) { active = slot; repaint(); } }
        void paint(juce::Graphics& g) override
        {
            const auto area = getLocalBounds().toFloat().reduced(0.5f);
            g.setColour(palette::plot);
            g.fillRoundedRectangle(area, 7.0f);
            g.setColour(palette::edge);
            g.drawRoundedRectangle(area, 7.0f, 1.0f);
            for (int index = 0; index < 3; ++index)
            {
                const auto cell = cellAt(index);
                if (index == active)
                {
                    g.setColour(palette::edge);
                    g.fillRoundedRectangle(cell.reduced(3.0f), 5.0f);
                }
                g.setColour(index == active ? palette::text : palette::muted);
                g.setFont(font(index < 2 ? 15.0f : 13.0f, Weight::bold));
                const juce::String words = index == 0 ? "A" : index == 1 ? "B"
                                         : juce::String(juce::CharPointer_UTF8(active == 0 ? "A\xe2\x86\x92" "B" : "B\xe2\x86\x92" "A"));
                g.drawText(words, cell, juce::Justification::centred);
            }
        }
        void mouseDown(const juce::MouseEvent& event) override
        {
            for (int index = 0; index < 3; ++index)
                if (cellAt(index).contains(event.position))
                {
                    if (index < 2 && onSelect) onSelect(index);
                    if (index == 2 && onCopy) onCopy();
                }
        }
    private:
        juce::Rectangle<float> cellAt(int index) const
        {
            const auto w = static_cast<float>(getWidth());
            const auto h = static_cast<float>(getHeight());
            return index == 0 ? juce::Rectangle<float>(0.0f, 0.0f, w * 0.3f, h)
                 : index == 1 ? juce::Rectangle<float>(w * 0.3f, 0.0f, w * 0.3f, h)
                              : juce::Rectangle<float>(w * 0.6f, 0.0f, w * 0.4f, h);
        }
        int active = 0;
    };

    class AdvancedButton final : public juce::Button
    {
    public:
        AdvancedButton() : juce::Button("ADVANCED") {}
        void paintButton(juce::Graphics& g, bool over, bool) override
        {
            PillToggle::paintPill(g, getLocalBounds().toFloat(), getToggleState(), over, palette::text,
                                  getToggleState() ? "HIDE ADVANCED" : "SHOW ADVANCED");
        }
    };

    bool showAdvanced() const { return static_cast<bool>(processor.params.state.getProperty("advancedView", false)); }

    void applyView()
    {
        const auto on = showAdvanced();
        advanced.setToggleState(on, juce::dontSendNotification);
        for (const auto index : { 0u, 1u, 5u, 6u })
        {
            knobs[index].setVisible(on);
            labels[index].setVisible(on);
        }
        guard.setVisible(on);
        delta.setVisible(on || plainValue(processor.params, "delta") > 0.5f);
        layoutKnobs();
        repaint();
    }

    void layoutKnobs()
    {
        const auto width = getWidth();
        const auto on = showAdvanced();
        const std::array<int, 7> sizes { 74, 74, on ? 92 : 104, on ? 132 : 140, on ? 92 : 104, 74, 74 };
        const std::array<float, 7> centres = on
            ? std::array<float, 7> { 0.075f, 0.195f, 0.335f, 0.5f, 0.665f, 0.805f, 0.925f }
            : std::array<float, 7> { 0.0f, 0.0f, 0.30f, 0.5f, 0.70f, 0.0f, 0.0f };
        for (std::size_t index = 0; index < knobs.size(); ++index)
        {
            const auto size = sizes[index];
            const auto hero = index == 3;
            const auto centreX = juce::roundToInt(static_cast<float>(width) * centres[index]);
            const auto top = hero ? controlsTop : controlsTop + (on ? 22 : 14);
            const auto textHeight = hero ? 26 : 22;
            knobs[index].setBounds(centreX - (size + 24) / 2, top, size + 24, size + textHeight);
            labels[index].setBounds(centreX - 60, knobs[index].getBottom(), 120, 20);
        }
    }

    void step(int direction)
    {
        const auto count = processor.getNumPrograms();
        processor.setCurrentProgram((processor.getCurrentProgram() + direction + count) % count);
        syncPreset();
    }

    EpsilonCarveAudioProcessor& processor;
    MeterHub hub;
    CollisionDisplay display;
    UnmaskFace face;
    ModeSwitch mode;
    PillToggle guard, delta, sidechain;
    AdvancedButton advanced;
    ABSwitch abSwitch;
    std::array<juce::Slider, 7> knobs;
    std::array<juce::Label, 7> labels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 7> attachments;
    juce::ComboBox presetBox;
    juce::TextButton previous { juce::CharPointer_UTF8("\xe2\x97\x80") }, next { juce::CharPointer_UTF8("\xe2\x96\xb6") };
    juce::TooltipWindow tooltips { this, 600 };
    static constexpr int controlsTop = 436;
};

//==============================================================================
EpsilonCarveAudioProcessorEditor::EpsilonCarveAudioProcessorEditor(EpsilonCarveAudioProcessor& p)
    : AudioProcessorEditor(&p),
      look(std::make_unique<CarveLook>()),
      panel(std::make_unique<Panel>(p, *look))
{
    setOpaque(true);
    addAndMakeVisible(*panel);
    panel->setBounds(0, 0, designWidth, designHeight);

    setResizable(true, true);
    if (auto* sizeLimits = getConstrainer())
    {
        sizeLimits->setFixedAspectRatio(static_cast<double>(designWidth) / designHeight);
        sizeLimits->setSizeLimits(designWidth * 85 / 100, designHeight * 85 / 100, designWidth * 2, designHeight * 2);
    }
    setSize(designWidth, designHeight);
    startTimerHz(4);
}

EpsilonCarveAudioProcessorEditor::~EpsilonCarveAudioProcessorEditor()
{
    stopTimer();
    panel = nullptr;
}

void EpsilonCarveAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(palette::background);
}

void EpsilonCarveAudioProcessorEditor::resized()
{
    const auto scale = static_cast<float>(getWidth()) / static_cast<float>(designWidth);
    panel->setTransform(juce::AffineTransform::scale(scale));
}

void EpsilonCarveAudioProcessorEditor::timerCallback()
{
    panel->syncPreset();
}
