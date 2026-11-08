#include "CarveEngine.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace carve
{
namespace
{
constexpr double pi = 3.14159265358979323846;
// Detectors are two cascaded Q 2.5 sections (4th order): ~-63 dB rejection four octaves
// away, so a loud tone no longer registers as trigger energy in distant bands.
constexpr float detectorDamping = 1.0f / 2.5f;
constexpr float prominenceDb = 36.0f;      // trigger bands this far below its loudest band are ignored
// Q 2: neighbouring sections overlap enough that a tone between two band centres is cut
// almost as deeply as a tone on a centre (v0.3.0 used Q 4.4 and varied by ~7 dB).
constexpr float eqDamping = 1.0f / 2.0f;
// Requested dB are spread over overlapping sections; this keeps the realised cut near the request.
constexpr float overlapCompensation = 2.1f;
constexpr float maxWeight = 0.93f;
constexpr float detectorScale = 5.0f;
constexpr float presenceFloorDb = -62.0f;   // trigger below this (after Sensitivity) carves nothing
constexpr float presenceKneeDb = 12.0f;
constexpr int controlInterval = 16;
constexpr float meterFramesPerSecond = 30.0f;
constexpr float bandIndexSpan = static_cast<float>(bandCount - 1);

float weightForCut(float cutDb) noexcept
{
    if (cutDb <= 0.0f)
        return 0.0f;
    const auto gain = std::pow(10.0f, -cutDb / (20.0f * overlapCompensation));
    return std::min(maxWeight, 1.0f - gain);
}

float toDb(float level) noexcept
{
    return 20.0f * std::log10(std::max(level, 1.0e-7f));
}
}

float frequencyAt(float position) noexcept
{
    return lowestBandHz * std::pow(highestBandHz / lowestBandHz, std::clamp(position, 0.0f, 1.0f));
}

float frequencyPosition(float frequencyHz) noexcept
{
    const auto frequency = std::clamp(frequencyHz, lowestBandHz, highestBandHz);
    return std::log(frequency / lowestBandHz) / std::log(highestBandHz / lowestBandHz);
}

float bandCentreHz(std::size_t index) noexcept
{
    return frequencyAt(static_cast<float>(index) / bandIndexSpan);
}

float Engine::Filter::bandpass(float input, const Coefficients& c) noexcept
{
    // Trapezoidal SVF (Simper) with fixed poles and unity bandpass gain at the centre.
    const auto v3 = input - integrator2;
    const auto v1 = c.a1 * integrator1 + c.a2 * v3;
    const auto v2 = integrator2 + c.a2 * integrator1 + c.a3 * v3;
    integrator1 = 2.0f * v1 - integrator1;
    integrator2 = 2.0f * v2 - integrator2;
    return c.damping * v1;
}

void Engine::prepare(double sampleRateHz) noexcept
{
    sampleRate = std::isfinite(sampleRateHz) && sampleRateHz > 0.0
                   ? static_cast<float>(sampleRateHz) : 44100.0f;
    samplesPerFrame = std::max(1, static_cast<int>(std::lround(sampleRate / meterFramesPerSecond)));
    cutSmoothing = 1.0f - std::exp(-1.0f / (sampleRate * 0.004f));
    guardSmoothing = 1.0f - std::exp(-1.0f / (sampleRate * 0.030f));
    bands = {};
    activeBands = 0;

    // At low host sample rates, leave above-bandwidth slots inactive instead of
    // moving several sections to the same frequency.
    for (std::size_t index = 0; index < bandCount; ++index)
    {
        const auto centre = bandCentreHz(index);
        if (centre > sampleRate * 0.475f)
            break;

        auto& band = bands[index];
        const auto g = static_cast<float>(std::tan(pi * centre / sampleRate));
        band.warpedCentre = g;
        // Keep each section's bandwidth in octaves constant up to Nyquist. Prewarping only
        // the centre squeezes high bands and left gaps above 8 kHz (RBJ bandwidth form).
        const auto omega = 2.0 * pi * centre / sampleRate;
        const auto stretch = omega / std::sin(omega);
        const auto coefficients = [g, stretch](float nominalDamping, double amountOfStretch)
        {
            const auto octaves = 2.0 / std::log(2.0) * std::asinh(0.5 * nominalDamping);
            const auto damping = static_cast<float>(2.0 * std::sinh(0.5 * std::log(2.0) * octaves * (1.0 + amountOfStretch * (stretch - 1.0))));
            const auto a1 = 1.0f / (1.0f + g * (g + damping));
            const auto a2 = g * a1;
            return Coefficients { a1, a2, g * a2, damping };
        };
        band.detector = coefficients(detectorDamping, 0.35);
        band.equalizer = coefficients(eqDamping, 1.0);
        band.eqDamping = band.equalizer.damping;
        ++activeBands;
    }
    reset();
}

void Engine::reset() noexcept
{
    for (auto& band : bands)
    {
        band.triggerState = {};
        band.targetState = {};
        band.triggerState2 = {};
        band.targetState2 = {};
        band.eqState = {};
        band.triggerEnvelope = band.targetEnvelope = 0.0f;
        band.cutWeight = band.desiredWeight = 0.0f;
    }
    window = {};
    samplesInWindow = 0;
    controlCountdown = 0;
    sideAmount = 1.0f;
}

void Engine::process(const TargetChannels& target, int numTargetChannels,
                     const TriggerChannels& sidechain, int numSidechainChannels,
                     int samples, const Settings& settings) noexcept
{
    if (numTargetChannels <= 0 || samples <= 0 || target[0] == nullptr)
        return;

    const auto useAux = settings.external && numSidechainChannels > 0 && sidechain[0] != nullptr;
    const auto source = useAux ? TriggerSource::external
                               : settings.external ? TriggerSource::unavailable : TriggerSource::self;
    const TriggerChannels trigger = useAux ? sidechain : TriggerChannels { target[0], target[1] };
    const auto triggerChannels = useAux ? numSidechainChannels : numTargetChannels;

    // Collision needs two different signals. Without a sidechain the target can only
    // carve itself, so the engine falls back to Classic behaviour.
    const auto collision = useAux && settings.mode == Mode::collision;

    // Each control band covers a log-frequency cell. Fractional overlap makes the
    // cutoffs continuous and lets two cutoffs isolate part of a cell.
    const auto low = frequencyPosition(settings.lowCutHz);
    const auto high = frequencyPosition(settings.highCutHz);
    for (std::size_t index = 0; index < bandCount; ++index)
    {
        const auto left = std::max(0.0f, (static_cast<float>(index) - 0.5f) / bandIndexSpan);
        const auto right = std::min(1.0f, (static_cast<float>(index) + 0.5f) / bandIndexSpan);
        rangeAmount[index] = index < activeBands
            ? std::clamp((std::min(high, right) - std::max(low, left)) / (right - left), 0.0f, 1.0f)
            : 0.0f;
    }

    const auto metering = meteringEnabled.load(std::memory_order_relaxed);
    if (metering)
    {
        if (window.source != source)
        {
            window = {};
            samplesInWindow = 0;
            window.source = source;
        }
    }
    else if (samplesInWindow != 0)
    {
        window = {};
        samplesInWindow = 0;
    }

    if (metering)
        collision ? processSamples<true, true>(target, numTargetChannels, trigger, triggerChannels, samples, settings, !useAux)
                  : processSamples<true, false>(target, numTargetChannels, trigger, triggerChannels, samples, settings, !useAux);
    else
        collision ? processSamples<false, true>(target, numTargetChannels, trigger, triggerChannels, samples, settings, !useAux)
                  : processSamples<false, false>(target, numTargetChannels, trigger, triggerChannels, samples, settings, !useAux);
}

void Engine::updateDesired(const Settings& settings, bool collision) noexcept
{
    const auto amount = std::clamp(settings.amountPercent * 0.01f, 0.0f, 1.0f);
    const auto sensitivity = std::pow(10.0f, settings.sensitivityDb / 20.0f);
    float loudestDb = presenceFloorDb;
    if (collision)
        for (std::size_t index = 0; index < activeBands; ++index)
            loudestDb = std::max(loudestDb, toDb(bands[index].triggerEnvelope) + settings.sensitivityDb);
    const auto gateDb = std::max(presenceFloorDb, loudestDb - prominenceDb - settings.sensitivityDb);
    for (std::size_t index = 0; index < activeBands; ++index)
    {
        auto& band = bands[index];
        const auto strength = amount * rangeAmount[index];
        if (strength <= 0.0f)
        {
            band.desiredWeight = 0.0f;
            continue;
        }

        // Classic: cut in proportion to how loud the trigger is in this band.
        auto cutDb = maxCutDb * std::min(1.0f, band.triggerEnvelope * sensitivity * detectorScale);
        if (collision)
        {
            // Collision: never deeper than Classic, and only as deep as needed for the trigger
            // to sit collisionClearanceDb (plus Sensitivity) above the target in this band.
            // Bands where the target is already quieter than the trigger are left untouched.
            const auto triggerDb = toDb(band.triggerEnvelope) + settings.sensitivityDb;
            const auto presence = std::clamp((triggerDb - gateDb) / presenceKneeDb, 0.0f, 1.0f);
            const auto needDb = toDb(band.targetEnvelope) - triggerDb
                                + collisionClearanceDb + 2.0f * settings.sensitivityDb;
            cutDb = std::min(cutDb, std::clamp(needDb, 0.0f, maxCutDb)) * presence;
        }
        band.desiredWeight = weightForCut(cutDb * strength);
    }
}

template <bool Metering, bool Collision>
void Engine::processSamples(const TargetChannels& target, int targetChannels,
                            const TriggerChannels& trigger, int triggerChannels, int samples,
                            const Settings& settings, bool selfTriggered) noexcept
{
    const auto mix = std::clamp(settings.mixPercent * 0.01f, 0.0f, 1.0f);
    const auto timing = [this](float milliseconds)
    {
        return 1.0f - std::exp(-1.0f / (std::max(1.0f, milliseconds) * 0.001f * sampleRate));
    };
    const auto attack = timing(settings.attackMs);
    const auto release = timing(settings.releaseMs);
    const auto guardTarget = settings.widthGuard ? 0.0f : 1.0f;

    const auto stereoTarget = targetChannels > 1 && target[1] != nullptr;
    const auto stereoTrigger = triggerChannels > 1 && trigger[1] != nullptr;
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto inputL = target[0][sample];
        const auto inputR = stereoTarget ? target[1][sample] : inputL;
        // Read the trigger before writing: in self mode it aliases the target.
        const auto keyL = trigger[0][sample];
        const auto keyR = stereoTrigger ? trigger[1][sample] : keyL;

        if (--controlCountdown <= 0)
        {
            updateDesired(settings, Collision);
            controlCountdown = controlInterval;
        }
        sideAmount += guardSmoothing * (guardTarget - sideAmount);

        if constexpr (Metering)
        {
            const auto peak = std::max(std::abs(keyL), std::abs(keyR));
            window.triggerInputPeak = std::max(window.triggerInputPeak, peak);
        }

        const auto mid = stereoTarget ? 0.5f * (inputL + inputR) : inputL;
        const auto side = stereoTarget ? 0.5f * (inputL - inputR) : 0.0f;
        auto wetMid = mid, wetSide = side;

        for (std::size_t index = 0; index < activeBands; ++index)
        {
            auto& band = bands[index];
            auto level = std::abs(band.triggerState2[0].bandpass(band.triggerState[0].bandpass(keyL, band.detector), band.detector));
            if (stereoTrigger)
                level = std::max(level, std::abs(band.triggerState2[1].bandpass(band.triggerState[1].bandpass(keyR, band.detector), band.detector)));
            band.triggerEnvelope += (level > band.triggerEnvelope ? attack : release)
                                    * (level - band.triggerEnvelope);

            if constexpr (Collision)
            {
                auto own = std::abs(band.targetState2[0].bandpass(band.targetState[0].bandpass(inputL, band.detector), band.detector));
                if (stereoTarget)
                    own = std::max(own, std::abs(band.targetState2[1].bandpass(band.targetState[1].bandpass(inputR, band.detector), band.detector)));
                band.targetEnvelope += (own > band.targetEnvelope ? attack : release)
                                       * (own - band.targetEnvelope);
            }
            else if (selfTriggered)
            {
                band.targetEnvelope = band.triggerEnvelope;
            }

            band.cutWeight += cutSmoothing * (band.desiredWeight - band.cutWeight);
            if (band.desiredWeight <= 0.0f && band.cutWeight < 1.0e-7f)
                band.cutWeight = 0.0f;

            wetMid -= band.cutWeight * band.eqState[0].bandpass(wetMid, band.equalizer);
            if (stereoTarget)
                wetSide -= band.cutWeight * sideAmount * band.eqState[1].bandpass(wetSide, band.equalizer);

            if constexpr (Metering)
            {
                auto& reading = window.bands[index];
                reading.triggerPeak = std::max(reading.triggerPeak, level);
                reading.targetPeak = std::max(reading.targetPeak, band.targetEnvelope);
            }
        }

        // Work with the removed part so a zero cut leaves the input bit-exact.
        const auto removedMid = mix * (mid - wetMid);
        const auto removedSide = mix * (side - wetSide);
        if (settings.delta)
        {
            // Delta: hear only what Carve takes out.
            target[0][sample] = removedMid + removedSide;
            if (stereoTarget)
                target[1][sample] = removedMid - removedSide;
        }
        else
        {
            target[0][sample] = inputL - (removedMid + removedSide);
            if (stereoTarget)
                target[1][sample] = inputR - (removedMid - removedSide);
        }

        if constexpr (Metering)
        {
            if (++samplesInWindow >= samplesPerFrame)
            {
                publishMeterFrame(settings);
                const auto source = window.source;
                window = {};
                window.source = source;
                samplesInWindow = 0;
            }
        }
    }
}

float cutResponseDb(const MeterFrame& frame, float frequencyHz, bool side) noexcept
{
    if (frequencyHz <= 0.0f || frequencyHz >= frame.sampleRate * 0.5f || frame.mix <= 0.0f)
        return 0.0f;

    const auto warped = std::tan(pi * frequencyHz / frame.sampleRate);
    std::complex<double> wet { 1.0, 0.0 };
    for (const auto& band : frame.bands)
    {
        const auto weight = side ? band.sideWeight : band.cutWeight;
        if (band.warpedCentre <= 0.0f || weight <= 0.0f)
            continue;

        const auto ratio = warped / band.warpedCentre;
        const auto dampingTerm = band.eqDamping * ratio;
        const std::complex<double> numerator { 0.0, dampingTerm };
        const std::complex<double> denominator { 1.0 - ratio * ratio, dampingTerm };
        wet *= 1.0 - static_cast<double>(weight) * numerator / denominator;
    }

    const auto mix = static_cast<double>(frame.mix);
    const auto magnitude = std::abs(1.0 - mix + mix * wet);
    return static_cast<float>(20.0 * std::log10(std::clamp(magnitude, 1.0e-6, 1.0)));
}

void Engine::setMeteringEnabled(bool enabled) noexcept
{
    meteringEnabled.store(enabled, std::memory_order_relaxed);
}

void Engine::publishMeterFrame(const Settings& settings) noexcept
{
    window.sampleRate = sampleRate;
    window.lowCutHz = settings.lowCutHz;
    window.highCutHz = settings.highCutHz;
    window.mix = std::clamp(settings.mixPercent * 0.01f, 0.0f, 1.0f);
    window.mode = settings.mode;
    window.widthGuard = settings.widthGuard;
    for (std::size_t index = 0; index < activeBands; ++index)
    {
        window.bands[index].cutWeight = bands[index].cutWeight;
        window.bands[index].sideWeight = bands[index].cutWeight * sideAmount;
        window.bands[index].warpedCentre = bands[index].warpedCentre;
        window.bands[index].eqDamping = bands[index].eqDamping;
    }
    meterSlots[writeIndex] = window;
    writeIndex = middle.exchange(writeIndex | readyBit, std::memory_order_acq_rel) & indexMask;
}

bool Engine::readMeterFrame(MeterFrame& frame) noexcept
{
    if ((middle.load(std::memory_order_acquire) & readyBit) == 0)
        return false;

    readIndex = middle.exchange(readIndex, std::memory_order_acq_rel) & indexMask;
    frame = meterSlots[readIndex];
    return true;
}
}
