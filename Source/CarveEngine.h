#pragma once

// Epsilon Carve DSP engine. Plain C++17, no JUCE dependency, no allocation after prepare().
//
// Signal flow (per sample, zero latency):
//   trigger (sidechain) -> 32 log-spaced detector bands -> envelope followers --+
//   target (main input) -> 32 detector bands (Collision mode only) ------------+-> cut per band (dB)
//   target -> Mid/Side -> 32 cascaded dynamic bell cuts (fixed poles) -> L/R -> wet/dry mix -> out
//
// Collision mode cuts a band only when the target is loud enough there to mask the trigger,
// and only as deep as needed to give the trigger its clearance. Classic mode cuts in
// proportion to the trigger level alone. Width Guard limits the cut to the Mid channel so
// the target's stereo image is left as it was.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace carve
{
constexpr std::size_t bandCount = 32;
constexpr std::size_t maxChannels = 2;
constexpr float lowestBandHz = 20.0f;
constexpr float highestBandHz = 20000.0f;
constexpr float maxCutDb = 18.0f;        // requested cut ceiling per band at 100% Amount
constexpr float collisionClearanceDb = 9.0f;

float frequencyAt(float position) noexcept;
float frequencyPosition(float frequencyHz) noexcept;
float bandCentreHz(std::size_t index) noexcept;

enum class TriggerSource
{
    external,
    self,
    unavailable
};

enum class Mode
{
    collision = 0,
    classic = 1
};

struct Settings
{
    float amountPercent = 25.0f;
    float sensitivityDb = 0.0f;
    float attackMs = 15.0f;
    float releaseMs = 180.0f;
    float mixPercent = 100.0f;
    float lowCutHz = lowestBandHz;
    float highCutHz = highestBandHz;
    bool external = true;
    Mode mode = Mode::collision;
    bool widthGuard = false;
    bool delta = false;
};

struct BandReading
{
    float triggerPeak = 0.0f;   // detector level of the trigger (linear)
    float triggerEnvelope = 0.0f; // smoothed trigger level (same ballistics as the track)
    float targetPeak = 0.0f;    // detector level of the target (linear)
    float cutWeight = 0.0f;     // live EQ weight on the Mid channel
    float sideWeight = 0.0f;    // live EQ weight on the Side channel
    float warpedCentre = 0.0f;
    float eqDamping = 0.5f;
};

struct MeterFrame
{
    std::array<BandReading, bandCount> bands {};
    float sampleRate = 44100.0f;
    float triggerInputPeak = 0.0f;
    float lowCutHz = lowestBandHz;
    float highCutHz = highestBandHz;
    float mix = 1.0f;
    TriggerSource source = TriggerSource::self;
    Mode mode = Mode::collision;
    bool widthGuard = false;
};

// Frozen complex response of the live EQ, including overlapping bands and Mix.
// side = false gives the Mid (centre) response, side = true the Side response.
// For the display and tests; never called from the audio callback.
float cutResponseDb(const MeterFrame& frame, float frequencyHz, bool side = false) noexcept;

class Engine
{
public:
    using TargetChannels = std::array<float*, maxChannels>;
    using TriggerChannels = std::array<const float*, maxChannels>;

    void prepare(double sampleRateHz) noexcept;
    void reset() noexcept;
    void process(const TargetChannels& target, int numTargetChannels,
                 const TriggerChannels& sidechain, int numSidechainChannels,
                 int samples, const Settings& settings) noexcept;

    // One audio producer and one editor consumer. The audio thread never waits.
    void setMeteringEnabled(bool enabled) noexcept;
    bool readMeterFrame(MeterFrame& frame) noexcept;

    // Most recent raw samples (track Mid before the cut, and the key) for a display FFT.
    // Lock-free: called from the editor while the audio thread keeps writing.
    static constexpr std::size_t analyserSize = 8192;
    int copyAnalyserSamples(float* track, float* key, int count) const noexcept;

private:
    struct Coefficients
    {
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, damping = 1.0f;
    };

    struct Filter
    {
        float integrator1 = 0.0f, integrator2 = 0.0f;
        float bandpass(float input, const Coefficients& coefficients) noexcept;
    };

    struct Band
    {
        Coefficients detector, equalizer;
        float warpedCentre = 0.0f, eqDamping = 0.5f;
        std::array<Filter, maxChannels> triggerState {}, triggerState2 {}, targetState {}, targetState2 {}, eqState {};
        float triggerEnvelope = 0.0f, targetEnvelope = 0.0f;
        float cutWeight = 0.0f, desiredWeight = 0.0f;
    };

    void updateDesired(const Settings& settings, bool collision) noexcept;

    template <bool Metering, bool Collision>
    void processSamples(const TargetChannels& target, int targetChannels,
                        const TriggerChannels& trigger, int triggerChannels, int samples,
                        const Settings& settings, bool selfTriggered) noexcept;
    void publishMeterFrame(const Settings& settings) noexcept;

    std::array<Band, bandCount> bands {};
    std::size_t activeBands = 0;
    float sampleRate = 44100.0f;
    float cutSmoothing = 0.0f;
    float guardSmoothing = 0.0f;
    float sideAmount = 1.0f;     // 1 = cut Side like Mid, 0 = Width Guard fully on
    int controlCountdown = 0;    // cut targets are recomputed every controlInterval samples
    std::array<float, bandCount> rangeAmount {};

    MeterFrame window;
    int samplesInWindow = 0;
    int samplesPerFrame = 1470;
    std::atomic<bool> meteringEnabled { false };

    // Each side owns one slot. The middle slot carries the most recent publication.
    static constexpr unsigned indexMask = 3;
    static constexpr unsigned readyBit = 4;
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    std::array<MeterFrame, 3> meterSlots {};
    unsigned writeIndex = 0;
    unsigned readIndex = 2;
    std::atomic<unsigned> middle { 1 };

    std::array<float, analyserSize> analyserTrack {}, analyserKey {};
    std::atomic<std::uint32_t> analyserWrite { 0 };
};
}
