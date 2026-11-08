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

class EpsilonCarveAudioProcessor : public juce::AudioProcessor,
                                   private juce::AudioProcessorValueTreeState::Listener
{
public:
    EpsilonCarveAudioProcessor();
    ~EpsilonCarveAudioProcessor() override;

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

    // Counted, so a host that opens a new editor before closing the old one keeps the meters running.
    void setMeteringEnabled(bool enabled) noexcept
    {
        const auto clients = (meteringClients += enabled ? 1 : -1);
        engine.setMeteringEnabled(clients > 0);
    }
    bool readMeterFrame(carve::MeterFrame& frame) noexcept { return engine.readMeterFrame(frame); }
    int copyAnalyserSamples(float* track, float* key, int count) const noexcept
    {
        return engine.copyAnalyserSamples(track, key, count);
    }
    double getCurrentSampleRate() const noexcept { return sampleRateForDisplay.load(); }

    static const std::vector<CarvePreset>& presets();

    // True once any control is moved after a preset was loaded (the preset bar shows an asterisk).
    bool isPresetModified() const noexcept { return presetModified.load(); }

    // A/B: two complete sets of settings to compare. Selecting a slot stores the current settings
    // in the active slot and recalls the other; both are saved with the session.
    int getActiveSlot() const noexcept { return activeSlot; }
    void selectSlot(int slot);
    void copyActiveSlotToOther();

    juce::AudioProcessorValueTreeState params;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void parameterChanged(const juce::String&, float) override;
    std::vector<float> captureSettings() const;
    void applySettings(const std::vector<float>& values);

    carve::Engine engine;
    std::atomic<double> sampleRateForDisplay { 44100.0 };
    std::atomic<int> meteringClients { 0 };
    int currentPreset = 0;
    std::atomic<bool> presetModified { false };
    bool suppressModified = false;
    std::array<std::vector<float>, 2> slots;
    int activeSlot = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpsilonCarveAudioProcessor)
};
