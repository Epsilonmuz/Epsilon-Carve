#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

const std::vector<CarvePreset>& EpsilonCarveAudioProcessor::presets()
{
    //                         amount sens  atk  rel   mix  low     high   mode guard
    static const std::vector<CarvePreset> list {
        { "Init",                25.0f, 0.0f, 15.0f, 180.0f, 100.0f,  20.0f, 20000.0f, 0, false },
        { "Vocal Space",         40.0f, 0.0f, 10.0f, 160.0f, 100.0f, 120.0f,  8000.0f, 0, true  },
        { "Rap Vocal Pocket",    50.0f, 3.0f,  5.0f, 120.0f, 100.0f, 150.0f,  6000.0f, 0, true  },
        { "Lead Over Beat",      35.0f, 0.0f, 12.0f, 200.0f, 100.0f, 250.0f,  7000.0f, 0, true  },
        { "Kick Through 808",    55.0f, 0.0f,  2.0f,  90.0f, 100.0f,  30.0f,   250.0f, 1, false },
        { "Snare Pocket",        35.0f, 0.0f,  1.0f,  80.0f, 100.0f, 150.0f,  5000.0f, 1, false },
        { "Clean Low Mids",      45.0f, 0.0f, 15.0f, 220.0f, 100.0f, 200.0f,   700.0f, 0, false },
        { "Voice-Over Bed",      70.0f, 6.0f,  5.0f, 300.0f, 100.0f, 100.0f, 10000.0f, 0, false },
        { "Air For The Hook",    35.0f, 0.0f,  8.0f, 150.0f, 100.0f, 3000.0f, 16000.0f, 0, true  },
        { "Gentle Glue",         15.0f, 0.0f, 20.0f, 250.0f, 100.0f,  20.0f, 20000.0f, 0, false },
    };
    return list;
}

juce::AudioProcessorValueTreeState::ParameterLayout EpsilonCarveAudioProcessor::makeLayout()
{
    // Parameter IDs from v0.3.0 are kept so saved sessions and automation still load.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto unit = [](const char* suffix, int decimals)
    {
        return juce::AudioParameterFloatAttributes {}.withStringFromValueFunction(
            [suffix, decimals](float value, int) { return juce::String(value, decimals) + suffix; });
    };
    auto add = [&layout](const char* id, const char* name, juce::NormalisableRange<float> range,
                         float initial, juce::AudioParameterFloatAttributes attributes)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 }, name, range, initial, attributes));
    };

    add("amount",      "Amount",      { 0.0f, 100.0f },          25.0f, unit("%", 0));
    add("sensitivity", "Sensitivity", { -48.0f, 24.0f },          0.0f, unit(" dB", 1));
    add("attack",      "Attack ms",   { 1.0f, 150.0f, 0.0f, 0.4f }, 15.0f, unit(" ms", 1));
    add("release",     "Release ms",  { 10.0f, 1000.0f, 0.0f, 0.4f }, 180.0f, unit(" ms", 0));
    add("mix",         "Mix",         { 0.0f, 100.0f },         100.0f, unit("%", 0));

    const juce::NormalisableRange<float> frequencyRange {
        carve::lowestBandHz, carve::highestBandHz,
        [](float, float, float position) { return carve::frequencyAt(position); },
        [](float, float, float frequency) { return carve::frequencyPosition(frequency); },
        [](float low, float high, float frequency) { return juce::jlimit(low, high, frequency); }
    };
    const auto frequencyText = juce::AudioParameterFloatAttributes {}
        .withStringFromValueFunction([](float frequency, int)
        {
            return frequency < 1000.0f ? juce::String(frequency, 0) + " Hz"
                                       : juce::String(frequency / 1000.0f, 2) + " kHz";
        })
        .withValueFromStringFunction([](const juce::String& input)
        {
            const auto frequency = input.getFloatValue();
            return input.trimEnd().endsWithIgnoreCase("k") || input.trimEnd().endsWithIgnoreCase("kHz")
                     ? 1000.0f * frequency : frequency;
        });
    add("low",  "Low Cut",  frequencyRange, carve::lowestBandHz,  frequencyText);
    add("high", "High Cut", frequencyRange, carve::highestBandHz, frequencyText);

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "external", 1 }, "External sidechain", true));
    // New in v0.4.0
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "mode", 1 }, "Mode", juce::StringArray { "Collision", "Classic" }, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "guard", 1 }, "Width Guard", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "delta", 1 }, "Delta Listen", false));
    return layout;
}

EpsilonCarveAudioProcessor::EpsilonCarveAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)
          .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      params(*this, nullptr, "PARAMETERS", makeLayout())
{
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            params.addParameterListener(ranged->getParameterID(), this);
}

EpsilonCarveAudioProcessor::~EpsilonCarveAudioProcessor()
{
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            params.removeParameterListener(ranged->getParameterID(), this);
}

void EpsilonCarveAudioProcessor::parameterChanged(const juce::String& id, float)
{
    // Delta is a listening aid, not part of a sound, so it never marks a preset as edited.
    if (!suppressModified && id != "delta")
        presetModified.store(true);
}

std::vector<float> EpsilonCarveAudioProcessor::captureSettings() const
{
    std::vector<float> values;
    for (auto* parameter : getParameters())
        values.push_back(parameter->getValue());
    return values;
}

void EpsilonCarveAudioProcessor::applySettings(const std::vector<float>& values)
{
    const juce::ScopedValueSetter<bool> quiet(suppressModified, true);
    const auto& list = getParameters();
    for (int index = 0; index < list.size() && index < static_cast<int>(values.size()); ++index)
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(list[index]))
            if (ranged->getParameterID() != "delta")
                ranged->setValueNotifyingHost(values[static_cast<std::size_t>(index)]);
}

void EpsilonCarveAudioProcessor::selectSlot(int slot)
{
    slot = juce::jlimit(0, 1, slot);
    if (slot == activeSlot)
        return;
    slots[static_cast<std::size_t>(activeSlot)] = captureSettings();
    auto& target = slots[static_cast<std::size_t>(slot)];
    if (target.empty())
        target = slots[static_cast<std::size_t>(activeSlot)];   // first visit: B starts as a copy of A
    activeSlot = slot;
    applySettings(target);
}

void EpsilonCarveAudioProcessor::copyActiveSlotToOther()
{
    slots[static_cast<std::size_t>(1 - activeSlot)] = captureSettings();
}

bool EpsilonCarveAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto main = layout.getMainInputChannelSet();
    if ((main != juce::AudioChannelSet::mono() && main != juce::AudioChannelSet::stereo())
        || layout.getMainOutputChannelSet() != main)
        return false;

    if (layout.inputBuses.size() < 2)
        return true;

    const auto side = layout.getChannelSet(true, 1);
    return side.isDisabled() || side == juce::AudioChannelSet::mono()
                            || side == juce::AudioChannelSet::stereo();
}

void EpsilonCarveAudioProcessor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate);
    sampleRateForDisplay.store(sampleRate);
    setLatencySamples(0);
}

void EpsilonCarveAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() == 0)
        return;

    auto value = [this](const char* id)
    {
        return params.getRawParameterValue(id)->load(std::memory_order_relaxed);
    };

    carve::Settings settings;
    settings.amountPercent = value("amount");
    settings.sensitivityDb = value("sensitivity");
    settings.attackMs = value("attack");
    settings.releaseMs = value("release");
    settings.mixPercent = value("mix");
    settings.lowCutHz = value("low");
    settings.highCutHz = value("high");
    settings.external = value("external") > 0.5f;
    settings.mode = value("mode") > 0.5f ? carve::Mode::classic : carve::Mode::collision;
    settings.widthGuard = value("guard") > 0.5f;
    settings.delta = value("delta") > 0.5f;

    auto main = getBusBuffer(buffer, false, 0);
    const auto mainChannels = juce::jmin(main.getNumChannels(), static_cast<int>(carve::maxChannels));
    carve::Engine::TargetChannels target {};
    for (int channel = 0; channel < mainChannels; ++channel)
        target[static_cast<std::size_t>(channel)] = main.getWritePointer(channel);

    carve::Engine::TriggerChannels trigger {};
    const auto sidechainChannels = settings.external && getBusCount(true) > 1
        ? juce::jmin(getChannelCountOfBus(true, 1), static_cast<int>(carve::maxChannels))
        : 0;
    if (sidechainChannels > 0)
    {
        auto sidechain = getBusBuffer(buffer, true, 1);
        for (int channel = 0; channel < sidechainChannels; ++channel)
            trigger[static_cast<std::size_t>(channel)] = sidechain.getReadPointer(channel);
    }

    engine.process(target, mainChannels, trigger, sidechainChannels, buffer.getNumSamples(), settings);

    // Clear any output channels the engine does not own.
    for (int channel = mainChannels; channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
}

int EpsilonCarveAudioProcessor::getNumPrograms()
{
    return static_cast<int>(presets().size());
}

const juce::String EpsilonCarveAudioProcessor::getProgramName(int index)
{
    return juce::isPositiveAndBelow(index, getNumPrograms()) ? presets()[static_cast<std::size_t>(index)].name
                                                             : juce::String();
}

void EpsilonCarveAudioProcessor::setCurrentProgram(int index)
{
    if (!juce::isPositiveAndBelow(index, getNumPrograms()))
        return;

    currentPreset = index;
    const juce::ScopedValueSetter<bool> quiet(suppressModified, true);
    presetModified.store(false);
    const auto& preset = presets()[static_cast<std::size_t>(index)];
    const auto set = [this](const char* id, float plainValue)
    {
        if (auto* parameter = params.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
    };
    set("amount", preset.amount);
    set("sensitivity", preset.sensitivity);
    set("attack", preset.attack);
    set("release", preset.release);
    set("mix", preset.mix);
    set("low", preset.low);
    set("high", preset.high);
    set("mode", static_cast<float>(preset.mode));
    set("guard", preset.widthGuard ? 1.0f : 0.0f);
}

juce::AudioProcessorEditor* EpsilonCarveAudioProcessor::createEditor()
{
    return new EpsilonCarveAudioProcessorEditor(*this);
}

void EpsilonCarveAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = params.copyState();
    state.setProperty("preset", currentPreset, nullptr);
    state.setProperty("presetModified", presetModified.load(), nullptr);
    for (auto child = state.getChildWithName("AB"); child.isValid(); child = state.getChildWithName("AB"))
        state.removeChild(child, nullptr);
    slots[static_cast<std::size_t>(activeSlot)] = captureSettings();
    juce::ValueTree ab("AB");
    ab.setProperty("active", activeSlot, nullptr);
    for (int slot = 0; slot < 2; ++slot)
    {
        juce::StringArray numbers;
        for (const auto value : slots[static_cast<std::size_t>(slot)])
            numbers.add(juce::String(value, 6));
        ab.setProperty(slot == 0 ? "a" : "b", numbers.joinIntoString(","), nullptr);
    }
    state.appendChild(ab, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

void EpsilonCarveAudioProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || !xml->hasTagName(params.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml(*xml);
    currentPreset = juce::jlimit(0, getNumPrograms() - 1, static_cast<int>(tree.getProperty("preset", 0)));

    slots = {};
    activeSlot = 0;
    if (auto ab = tree.getChildWithName("AB"); ab.isValid())
    {
        activeSlot = juce::jlimit(0, 1, static_cast<int>(ab.getProperty("active", 0)));
        for (int slot = 0; slot < 2; ++slot)
        {
            const auto text = ab.getProperty(slot == 0 ? "a" : "b").toString();
            if (text.isNotEmpty())
                for (const auto& number : juce::StringArray::fromTokens(text, ",", {}))
                    slots[static_cast<std::size_t>(slot)].push_back(number.getFloatValue());
        }
        tree.removeChild(ab, nullptr);
    }

    {
        const juce::ScopedValueSetter<bool> quiet(suppressModified, true);
        params.replaceState(tree);

        // replaceState skips parameters whose stored plain value already matches, so a bool or
        // choice left at an in-between normalised value would not snap. Re-sync every parameter.
        for (auto* parameter : getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            {
                const auto stored = ranged->convertTo0to1(ranged->convertFrom0to1(ranged->getValue()));
                if (!juce::exactlyEqual(stored, ranged->getValue()))
                    ranged->setValueNotifyingHost(stored);
            }
    }
    presetModified.store(static_cast<bool>(tree.getProperty("presetModified", false)));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EpsilonCarveAudioProcessor();
}
