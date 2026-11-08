#pragma once

// Reads the engine once per frame (meters, analyser FFT, cut curve, face masking) for the display and the face.

#include "CarveUI.h"

namespace carveui
{
//==============================================================================
// Reads the engine once per frame and shares the result with the display and the face.
class MeterHub final : private juce::Timer
{
public:
    explicit MeterHub(EpsilonCarveAudioProcessor& p) : processor(p), fft(fftOrder),
          window(static_cast<size_t>(fftSize), juce::dsp::WindowingFunction<float>::hann, false)
    {
        processor.readMeterFrame(frame);
        processor.setMeteringEnabled(true);
        regionPresence.fill(1.0f);
        regionTarget.fill(1.0f);
        trackDb.fill(spectrumFloorDb);
        keyDb.fill(spectrumFloorDb);
        startTimerHz(30);
    }

    ~MeterHub() override
    {
        stopTimer();
        processor.setMeteringEnabled(false);
    }

    std::function<void()> onUpdate;

    carve::MeterFrame frame;
    bool hasFrame = false;
    std::array<float, tracePoints> trackDb {}, keyDb {}, cutDb {}, sideCutDb {};
    std::array<float, faceRegions> regionPresence {}, regionTarget {}, regionActivity {};
    std::array<bool, faceRegions> regionActive {};
    float clarity = 0.0f, clarityShown = 0.0f;
    bool vocalPresent = false;
    float maxCut = 0.0f, maxCutFrequency = 0.0f;
    float pulse = 0.0f;   // 0..1 slow oscillator for Delta warnings

private:
    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounter();
        pulse = 0.5f + 0.5f * std::sin(static_cast<float>(now) * 0.006f);
        if (processor.readMeterFrame(frame))
        {
            hasFrame = true;
            lastFrameMs = now;
            computeCut();
            computeSpectra();
            computeMasking();
        }
        else if (hasFrame && now - lastFrameMs > 300)
        {
            hasFrame = false;
            trackDb.fill(spectrumFloorDb);
            keyDb.fill(spectrumFloorDb);
            cutDb.fill(0.0f);
            sideCutDb.fill(0.0f);
            maxCut = 0.0f;
        }

        // Shards lift quickly as a region clears and settle back more slowly.
        for (std::size_t r = 0; r < faceRegions; ++r)
        {
            const auto rate = regionTarget[r] < regionPresence[r] ? 0.22f : 0.08f;
            regionPresence[r] += rate * (regionTarget[r] - regionPresence[r]);
            regionActivity[r] += 0.12f * ((regionActive[r] ? 1.0f : 0.0f) - regionActivity[r]);
        }
        clarityShown += 0.15f * (clarity - clarityShown);

        if (onUpdate)
            onUpdate();
    }

    void computeCut()
    {
        maxCut = 0.0f;
        for (int index = 0; index < tracePoints; ++index)
        {
            const auto frequency = carve::frequencyAt(static_cast<float>(index) / static_cast<float>(tracePoints - 1));
            const auto mid = carve::cutResponseDb(frame, frequency, false);
            cutDb[static_cast<std::size_t>(index)] = mid;
            sideCutDb[static_cast<std::size_t>(index)] = frame.widthGuard ? carve::cutResponseDb(frame, frequency, true) : mid;
            if (mid < maxCut)
            {
                maxCut = mid;
                maxCutFrequency = frequency;
            }
        }
    }

    void computeSpectra()
    {
        // High-resolution analyser: 4096-point FFT, 1/6-octave smoothing, ~25 dB/s fall.
        std::array<float, fftSize> trackSamples {}, keySamples {};
        processor.copyAnalyserSamples(trackSamples.data(), keySamples.data(), fftSize);
        const auto sampleRate = static_cast<float>(processor.getCurrentSampleRate());
        const auto analyse = [&](const std::array<float, fftSize>& samples, std::array<float, tracePoints>& out)
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            std::copy(samples.begin(), samples.end(), buffer.begin());
            window.multiplyWithWindowingTable(buffer.data(), static_cast<size_t>(fftSize));
            fft.performFrequencyOnlyForwardTransform(buffer.data(), true);
            const auto binHz = sampleRate / static_cast<float>(fftSize);
            const auto scale = 4.0f / static_cast<float>(fftSize);   // Hann coherent gain 0.5, one-sided
            for (int index = 0; index < tracePoints; ++index)
            {
                const auto centre = carve::frequencyAt(static_cast<float>(index) / static_cast<float>(tracePoints - 1));
                // 1/6-octave window: average power across the bins it covers; below ~200 Hz,
                // where the window is narrower than two bins, interpolate between bins instead.
                const auto lowEdge = centre * 0.944f / binHz, highEdge = centre * 1.059f / binHz;
                float power = 0.0f;
                if (highEdge - lowEdge < 2.0f)
                {
                    const auto exact = juce::jlimit(1.0f, static_cast<float>(fftSize / 2 - 2), centre / binHz);
                    const auto bin = static_cast<int>(exact);
                    const auto frac = exact - static_cast<float>(bin);
                    power = scale * juce::jmap(frac, buffer[static_cast<size_t>(bin)], buffer[static_cast<size_t>(bin + 1)]);
                }
                else
                {
                    const auto lowBin = juce::jlimit(1, fftSize / 2 - 1, static_cast<int>(lowEdge));
                    const auto highBin = juce::jlimit(lowBin, fftSize / 2 - 1, static_cast<int>(std::ceil(highEdge)));
                    double sum = 0.0;
                    for (int bin = lowBin; bin <= highBin; ++bin)
                        sum += static_cast<double>(buffer[static_cast<size_t>(bin)]) * buffer[static_cast<size_t>(bin)];
                    // Sum of the window's power, so a tone and a wide band read on the same scale.
                    power = scale * static_cast<float>(std::sqrt(sum));
                }
                const auto db = juce::Decibels::gainToDecibels(power, spectrumFloorDb);
                auto& shown = out[static_cast<std::size_t>(index)];
                shown = db > shown ? db : juce::jmax(db, shown - 0.85f);
            }
        };
        analyse(trackSamples, trackDb);
        analyse(keySamples, keyDb);
    }

    void computeMasking()
    {
        vocalPresent = false;
        if (frame.source != carve::TriggerSource::external)
            return;

        float loudest = -200.0f;
        std::array<float, carve::bandCount> trigger {}, target {}, cut {};
        for (std::size_t b = 0; b < carve::bandCount; ++b)
        {
            const auto& band = frame.bands[b];
            if (band.warpedCentre <= 0.0f)
            {
                trigger[b] = -200.0f;
                continue;
            }
            trigger[b] = juce::Decibels::gainToDecibels(band.triggerEnvelope, -200.0f);
            target[b] = juce::Decibels::gainToDecibels(band.targetPeak, -200.0f);
            cut[b] = carve::cutResponseDb(frame, carve::bandCentreHz(b), false);
            loudest = juce::jmax(loudest, trigger[b]);
        }
        if (loudest < -60.0f)
            return;   // vocal silent: hold the face as it was

        vocalPresent = true;
        std::array<float, faceRegions> weight {}, masked {}, maskedBefore {};
        float totalWeight = 0.0f, totalMasked = 0.0f;
        for (std::size_t b = 0; b < carve::bandCount; ++b)
        {
            const auto w = juce::jlimit(0.0f, 1.0f, (trigger[b] - (loudest - 36.0f)) / 36.0f);
            if (w <= 0.0f)
                continue;
            // 0 = the sidechain is clear (track 9 dB or more below it), 1 = buried (track 3 dB or more
            // above it). The clear point matches Collision mode's 9 dB clearance target.
            const auto buried = [&](float cutDbHere)
            {
                return juce::jlimit(0.0f, 1.0f, (target[b] + cutDbHere - trigger[b] + 9.0f) / 12.0f);
            };
            const auto d = buried(cut[b]), before = buried(0.0f);
            totalWeight += w;
            totalMasked += w * d;
            // A band covers a slice of the log axis; it counts towards each face region by how much
            // of that slice falls inside the region, so a band on a boundary is shared, not rounded.
            const auto cellLow = juce::jmax(0.0f, (static_cast<float>(b) - 0.5f) / 31.0f);
            const auto cellHigh = juce::jmin(1.0f, (static_cast<float>(b) + 0.5f) / 31.0f);
            for (std::size_t r = 0; r < faceRegions; ++r)
            {
                const auto overlap = juce::jmax(0.0f, juce::jmin(cellHigh, regionHighPos(r)) - juce::jmax(cellLow, regionLowPos(r)))
                                     / (cellHigh - cellLow);
                if (overlap <= 0.0f)
                    continue;
                weight[r] += w * overlap;
                masked[r] += w * overlap * d;
                maskedBefore[r] += w * overlap * before;
            }
        }
        if (totalWeight <= 0.0f)
            return;

        // Clarity is absolute: how much of the sidechain is clear of the track right now.
        clarity = 1.0f - totalMasked / totalWeight;
        // Each shard shows only its own range: the share of that range's original masking that is
        // still left (1 = untouched, 0 = all removed). A range where the sidechain has almost nothing
        // is marked inactive and drawn as a faint outline instead of borrowing a value from elsewhere.
        for (std::size_t r = 0; r < faceRegions; ++r)
        {
            regionActive[r] = weight[r] > 0.15f;
            if (!regionActive[r])
                continue;
            regionTarget[r] = maskedBefore[r] > 0.02f * weight[r]
                                ? juce::jlimit(0.0f, 1.0f, masked[r] / maskedBefore[r]) : 0.0f;
        }
    }

    EpsilonCarveAudioProcessor& processor;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, fftSize * 2> buffer {};
    juce::uint32 lastFrameMs = 0;
};
}
