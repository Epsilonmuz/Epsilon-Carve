#pragma once

#include "PluginProcessor.h"

class CarveLook;
class CollisionDisplay;
class ModeSwitch;

// The whole panel is laid out at a fixed design size and scaled as one piece,
// so it reads the same at every window size.
class EpsilonCarveAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EpsilonCarveAudioProcessorEditor(EpsilonCarveAudioProcessor&);
    ~EpsilonCarveAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth = 920;
    static constexpr int designHeight = 610;

private:
    class Panel;
    void timerCallback() override;

    EpsilonCarveAudioProcessor& processor;
    std::unique_ptr<CarveLook> look;
    std::unique_ptr<Panel> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpsilonCarveAudioProcessorEditor)
};
