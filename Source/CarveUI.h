#pragma once

// Shared look of the Epsilon Carve panel: palette, typeface, small helpers and the face's
// frequency slices. Header-only; included by the component headers and PluginEditor.cpp.

#include "PluginProcessor.h"
#include "../Shared/epsilon_ui/EpsilonHeader.h"

#include <BinaryData.h>
#include <cmath>

namespace carveui
{
namespace palette
{
// Epsilon Sounds design system v1 (Epsilon tier). Track / key / collision stay: they are the hero (mask and face).
inline const epsilon_ui::Palette brand = epsilon_ui::palette (epsilon_ui::Tier::Epsilon);
inline const juce::Colour background = brand.bg;
inline const juce::Colour panel      = brand.surface;
inline const juce::Colour plot       = brand.bg.darker (0.15f);
inline const juce::Colour edge       = brand.line;
inline const juce::Colour text       = brand.ink;
inline const juce::Colour muted      = brand.inkMuted;
inline const juce::Colour dim        = epsilon_ui::faint (brand);
inline const juce::Colour track      { 0xfff2a541 };   // the signal being carved (the beat) - and the mask
inline const juce::Colour key        { 0xff3fd0ff };   // the sidechain (the vocal) - and the face
inline const juce::Colour collision  { 0xffff4d6d };   // where they collide and Carve cuts
}

enum class Weight { medium, semibold, bold };

inline juce::Font font(float height, Weight weight = Weight::medium)
{
    // Epsilon Dial, the Epsilon Sounds house typeface, embedded so the panel looks the same everywhere.
    static const auto medium = juce::Typeface::createSystemTypefaceFor(
        BinaryData::EpsilonDialRegular_ttf, BinaryData::EpsilonDialRegular_ttfSize);
    static const auto semibold = juce::Typeface::createSystemTypefaceFor(
        BinaryData::EpsilonDialBold_ttf, BinaryData::EpsilonDialBold_ttfSize);
    static const auto bold = juce::Typeface::createSystemTypefaceFor(
        BinaryData::EpsilonDialBold_ttf, BinaryData::EpsilonDialBold_ttfSize);
    const auto& face = weight == Weight::bold ? bold : weight == Weight::semibold ? semibold : medium;
    return juce::Font(juce::FontOptions(face).withHeight(height));
}

inline juce::String frequencyText(float frequency, int decimals)
{
    return frequency < 1000.0f ? juce::String(juce::roundToInt(frequency)) + " Hz"
                               : juce::String(frequency * 0.001f, decimals) + " kHz";
}

inline float plainValue(const juce::AudioProcessorValueTreeState& state, const char* id)
{
    return state.getRawParameterValue(id)->load(std::memory_order_relaxed);
}

constexpr float spectrumFloorDb = -84.0f;
constexpr float spectrumTopDb = 0.0f;
constexpr float cutFloorDb = -18.0f;
constexpr int tracePoints = 240;
constexpr int fftOrder = 12;
constexpr int fftSize = 1 << fftOrder;
constexpr int faceRegions = 8;
constexpr float rowTop = 26.0f, rowHeight = 24.5f;   // face design space: rows from 26 to 222
// The face is a frequency axis, top to bottom on a log scale: eight equal slices of 20 Hz-20 kHz
// (1.25 octaves each). Region 0 (forehead) = the highest slice, region 7 (chin) = the lowest.
inline float regionLowPos(std::size_t region) noexcept { return static_cast<float>(faceRegions - 1 - region) / faceRegions; }
inline float regionHighPos(std::size_t region) noexcept { return static_cast<float>(faceRegions - region) / faceRegions; }
inline juce::String edgeLabel(float position)
{
    const auto f = carve::frequencyAt(position);
    if (f >= 9950.0f) return juce::String(juce::roundToInt(f / 1000.0f)) + "k";
    if (f >= 1000.0f) return juce::String(f / 1000.0f, 1) + "k";
    return juce::String(juce::roundToInt(f));
}

inline const std::array<const char*, 7> sourceNames { "VOCAL", "KICK", "SNARE", "808 / BASS", "LEAD", "VOICE-OVER", "SIDECHAIN" };
inline const std::array<const char*, 7> sourceWords { "vocal", "kick", "snare", "808", "lead", "voice", "sidechain" };
inline const std::array<const char*, 7> sourceHints { "try Vocal Space or Rap Vocal Pocket", "try Kick Through 808 (Classic)",
                                               "try Snare Pocket (Classic)", "try Clean Low Mids, then set the range",
                                               "try Lead Over Beat", "try Voice-Over Bed", "any preset" };
inline int sourceIndex(const juce::AudioProcessorValueTreeState& state)
{
    return juce::jlimit(0, 6, static_cast<int>(state.state.getProperty("sourceName", 0)));
}
}
