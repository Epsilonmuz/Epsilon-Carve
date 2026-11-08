#pragma once

#include <JuceHeader.h>

#include "CarveEngine.h"

struct CarvePreset
{
    const char* name;
    float amount, sensitivity, attack, release, mix, low, high;
    int mode;          // 0 Collision, 1 Classic
    bool widthGuard;
};

class EpsilonCarveAudioProcessor : public juce::AudioProcessor
{
public:
    EpsilonCarveAudioProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Epsilon Carve"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int size) override;

    void setMeteringEnabled(bool enabled) noexcept { engine.setMeteringEnabled(enabled); }
    bool readMeterFrame(carve::MeterFrame& frame) noexcept { return engine.readMeterFrame(frame); }

    static const std::vector<CarvePreset>& presets();

    juce::AudioProcessorValueTreeState params;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();

    carve::Engine engine;
    int currentPreset = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpsilonCarveAudioProcessor)
};
