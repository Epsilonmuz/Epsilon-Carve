#pragma once

#include "PluginProcessor.h"

class CarveLook;

// The whole panel is laid out at a fixed design size and scaled as one piece,
// so it reads the same at every window size.
class EpsilonCarveAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EpsilonCarveAudioProcessorEditor(EpsilonCarveAudioProcessor&);
    ~EpsilonCarveAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth = 960;
    static constexpr int designHeight = 620;

private:
    class Panel;
    void timerCallback() override;

    std::unique_ptr<CarveLook> look;
    std::unique_ptr<Panel> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpsilonCarveAudioProcessorEditor)
};
