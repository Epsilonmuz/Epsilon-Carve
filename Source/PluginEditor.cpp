#include "PluginEditor.h"

#include <BinaryData.h>
#include <cmath>

namespace
{
namespace palette
{
const juce::Colour background { 0xff090b0f };
const juce::Colour panel      { 0xff10141b };
const juce::Colour plot       { 0xff0b0e13 };
const juce::Colour edge       { 0xff212937 };
const juce::Colour text       { 0xffeef2f7 };
const juce::Colour muted      { 0xff8b97aa };
const juce::Colour dim        { 0xff4a5568 };
const juce::Colour track      { 0xfff2a541 };   // the signal being carved (the beat) - and the mask
const juce::Colour key        { 0xff3fd0ff };   // the sidechain (the vocal) - and the face
const juce::Colour collision  { 0xffff4d6d };   // where they collide and Carve cuts
}

enum class Weight { medium, semibold, bold };

juce::Font font(float height, Weight weight = Weight::medium)
{
    // Barlow Semi Condensed (SIL Open Font License), embedded so the panel looks the same everywhere.
    static const auto medium = juce::Typeface::createSystemTypefaceFor(
        BinaryData::BarlowSemiCondensedMedium_ttf, BinaryData::BarlowSemiCondensedMedium_ttfSize);
    static const auto semibold = juce::Typeface::createSystemTypefaceFor(
        BinaryData::BarlowSemiCondensedSemiBold_ttf, BinaryData::BarlowSemiCondensedSemiBold_ttfSize);
    static const auto bold = juce::Typeface::createSystemTypefaceFor(
        BinaryData::BarlowSemiCondensedBold_ttf, BinaryData::BarlowSemiCondensedBold_ttfSize);
    const auto& face = weight == Weight::bold ? bold : weight == Weight::semibold ? semibold : medium;
    return juce::Font(juce::FontOptions(face).withHeight(height));
}

juce::String frequencyText(float frequency, int decimals)
{
    return frequency < 1000.0f ? juce::String(juce::roundToInt(frequency)) + " Hz"
                               : juce::String(frequency * 0.001f, decimals) + " kHz";
}

float plainValue(const juce::AudioProcessorValueTreeState& state, const char* id)
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
float regionLowPos(std::size_t region) noexcept { return static_cast<float>(faceRegions - 1 - region) / faceRegions; }
float regionHighPos(std::size_t region) noexcept { return static_cast<float>(faceRegions - region) / faceRegions; }
juce::String edgeLabel(float position)
{
    const auto f = carve::frequencyAt(position);
    if (f >= 9950.0f) return juce::String(juce::roundToInt(f / 1000.0f)) + "k";
    if (f >= 1000.0f) return juce::String(f / 1000.0f, 1) + "k";
    return juce::String(juce::roundToInt(f));
}

const std::array<const char*, 7> sourceNames { "VOCAL", "KICK", "SNARE", "808 / BASS", "LEAD", "VOICE-OVER", "SIDECHAIN" };
const std::array<const char*, 7> sourceWords { "vocal", "kick", "snare", "808", "lead", "voice", "sidechain" };
const std::array<const char*, 7> sourceHints { "try Vocal Space or Rap Vocal Pocket", "try Kick Through 808 (Classic)",
                                               "try Snare Pocket (Classic)", "try Clean Low Mids, then set the range",
                                               "try Lead Over Beat", "try Voice-Over Bed", "any preset" };
int sourceIndex(const juce::AudioProcessorValueTreeState& state)
{
    return juce::jlimit(0, 6, static_cast<int>(state.state.getProperty("sourceName", 0)));
}
}

//==============================================================================
class CarveLook final : public juce::LookAndFeel_V4
{
public:
    CarveLook()
    {
        setColour(juce::ComboBox::backgroundColourId, palette::plot);
        setColour(juce::ComboBox::outlineColourId, palette::edge);
        setColour(juce::ComboBox::textColourId, palette::text);
        setColour(juce::ComboBox::arrowColourId, palette::muted);
        setColour(juce::PopupMenu::backgroundColourId, palette::panel);
        setColour(juce::PopupMenu::textColourId, palette::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, palette::edge);
        setColour(juce::PopupMenu::highlightedTextColourId, palette::text);
        setColour(juce::Slider::textBoxTextColourId, palette::text);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, palette::edge);
        setColour(juce::TextEditor::backgroundColourId, palette::plot);
        setColour(juce::TextEditor::textColourId, palette::text);
        setColour(juce::TextEditor::focusedOutlineColourId, palette::edge);
        setColour(juce::CaretComponent::caretColourId, palette::text);
        setColour(juce::TooltipWindow::backgroundColourId, palette::panel);
        setColour(juce::TooltipWindow::textColourId, palette::text);
        setColour(juce::TooltipWindow::outlineColourId, palette::edge);
        setColour(juce::TextButton::buttonColourId, palette::plot);
        setColour(juce::TextButton::textColourOffId, palette::muted);
    }

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& f) override
    {
        return font(f.getHeight(), f.isBold() ? Weight::bold : Weight::medium).getTypefacePtr();
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float value, float start, float end, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                   static_cast<float>(width), static_cast<float>(height));
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 4.0f;
        if (radius <= 4.0f)
            return;

        const auto centre = bounds.getCentre();
        const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const auto hero = static_cast<bool>(slider.getProperties()["hero"]);
        const auto angle = start + value * (end - start);
        const auto ring = radius - 2.0f;
        const auto thickness = hero ? 5.0f : 3.0f;

        // Tick ring
        for (int tick = 0; tick <= 20; ++tick)
        {
            const auto tickAngle = start + (end - start) * static_cast<float>(tick) / 20.0f;
            const auto inner = centre.getPointOnCircumference(ring + thickness * 0.5f + 2.0f, tickAngle);
            const auto outer = centre.getPointOnCircumference(ring + thickness * 0.5f + (tick % 5 == 0 ? 6.0f : 4.0f), tickAngle);
            g.setColour(palette::edge.brighter(tick % 5 == 0 ? 0.35f : 0.0f));
            g.drawLine(juce::Line<float>(inner, outer), 1.0f);
        }

        juce::Path rail, active;
        rail.addCentredArc(centre.x, centre.y, ring, ring, 0.0f, start, end, true);
        active.addCentredArc(centre.x, centre.y, ring, ring, 0.0f, start, angle, true);
        const juce::PathStrokeType stroke(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour(palette::edge);
        g.strokePath(rail, stroke);
        if (hero)
        {
            g.setColour(accent.withAlpha(0.18f));
            g.strokePath(active, juce::PathStrokeType(thickness + 9.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }
        g.setColour(accent);
        g.strokePath(active, stroke);

        // Body: machined puck with a light catch across the top
        const auto bodyRadius = ring - thickness - (hero ? 8.0f : 5.0f);
        const auto body = juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre);
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillEllipse(body.translated(0.0f, 3.0f).expanded(1.5f));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2c3441), body.getX(), body.getY(),
                                               juce::Colour(0xff10141a), body.getRight(), body.getBottom(), false));
        g.fillEllipse(body);
        const auto inset = body.reduced(bodyRadius * 0.16f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1a1f28), inset.getX(), inset.getBottom(),
                                               juce::Colour(0xff252c37), inset.getX(), inset.getY(), false));
        g.fillEllipse(inset);
        juce::Path catchLight;
        catchLight.addCentredArc(centre.x, centre.y, bodyRadius - 1.0f, bodyRadius - 1.0f, 0.0f, -2.3f, 0.7f, true);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.strokePath(catchLight, juce::PathStrokeType(1.4f));
        g.setColour(palette::edge.brighter(0.3f));
        g.drawEllipse(body, 1.0f);

        const auto inner = centre.getPointOnCircumference(bodyRadius * 0.34f, angle);
        const auto outer = centre.getPointOnCircumference(bodyRadius * 0.86f, angle);
        g.setColour(hero ? accent : palette::text);
        g.drawLine(juce::Line<float>(inner, outer), hero ? 3.2f : 2.2f);
    }

    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* label = LookAndFeel_V4::createSliderTextBox(slider);
        label->setFont(font(static_cast<bool>(slider.getProperties()["hero"]) ? 20.0f : 15.0f, Weight::semibold));
        label->setJustificationType(juce::Justification::centred);
        return label;
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                      juce::ComboBox& box) override
    {
        const auto area = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        g.setColour(palette::plot);
        g.fillRoundedRectangle(area, 6.0f);
        g.setColour(box.isMouseOver(true) ? palette::dim : palette::edge);
        g.drawRoundedRectangle(area.reduced(0.5f), 6.0f, 1.0f);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(box.getLocalBounds().reduced(8, 1));
        label.setFont(font(17.0f, Weight::semibold));
        label.setJustificationType(juce::Justification::centred);
    }

    juce::Font getPopupMenuFont() override { return font(16.0f); }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down) override
    {
        const auto area = button.getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(down ? palette::edge : palette::plot);
        g.fillRoundedRectangle(area, 6.0f);
        g.setColour(over ? palette::dim : palette::edge);
        g.drawRoundedRectangle(area, 6.0f, 1.0f);
    }
};

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

//==============================================================================
// The face is the vocal, the mask is the beat. Each shard covers one frequency region and
// lifts away as Carve clears that region.
class UnmaskFace final : public juce::Component, public juce::SettableTooltipClient
{
public:
    UnmaskFace(MeterHub& meterHub, juce::AudioProcessorValueTreeState& state, juce::LookAndFeel& look)
        : hub(meterHub), params(state)
    {
        setOpaque(true);
        setTooltip("The face is your sidechain: the sound that needs room. The mask is the track covering it.\n"
                   "The face is a frequency scale: 20 kHz at the top, 20 Hz at the chin, eight equal slices.\n"
                   "Each shard lifts by exactly how much of its slice's masking Carve has removed.");
        buildArt();
        source.setLookAndFeel(&look);
        source.setTooltip("What is in the sidechain? This only changes the labels.");
        source.onClick = [this] { showSourceMenu(); };
        addAndMakeVisible(source);
        refreshSource();
    }

    ~UnmaskFace() override { source.setLookAndFeel(nullptr); }

    void resized() override { source.setBounds(getWidth() - 16 - 128, 9, 128, 24); }

    void refreshSource()
    {
        const auto index = sourceIndex(params);
        source.setButtonText(juce::String(sourceNames[static_cast<std::size_t>(index)]) + juce::String(juce::CharPointer_UTF8("  \xe2\x96\xbe")));
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(palette::background);
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(palette::panel);
        g.fillRoundedRectangle(bounds, 12.0f);
        g.setColour(palette::edge);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 12.0f, 1.0f);

        g.setColour(palette::text);
        g.setFont(font(15.0f, Weight::bold));
        g.drawText("UNMASK", juce::Rectangle<float>(16.0f, 10.0f, 120.0f, 22.0f), juce::Justification::centredLeft);

        const auto external = hub.frame.source == carve::TriggerSource::external && hub.hasFrame;
        const auto delta = plainValue(params, "delta") > 0.5f;

        // Art box (design space 200 x 250) fitted into the panel
        const auto artArea = bounds.reduced(14.0f).withTrimmedTop(26.0f).withTrimmedBottom(54.0f);
        const auto scale = juce::jmin(artArea.getWidth() / 200.0f, artArea.getHeight() / 250.0f);
        const auto art = juce::AffineTransform::scale(scale)
                             .translated(artArea.getCentreX() - 100.0f * scale, artArea.getCentreY() - 125.0f * scale);

        // Halo behind the face grows with clarity
        const auto clarity = external ? hub.clarityShown : 0.0f;
        const auto haloCentre = juce::Point<float>(100.0f, 120.0f).transformedBy(art);
        g.setGradientFill(juce::ColourGradient(palette::key.withAlpha(0.05f + 0.22f * clarity), haloCentre,
                                               palette::key.withAlpha(0.0f), haloCentre.translated(0.0f, 120.0f * scale), true));
        g.fillEllipse(juce::Rectangle<float>(250.0f * scale, 250.0f * scale).withCentre(haloCentre));

        paintFace(g, art, scale, external ? 1.0f : 0.45f);
        paintMask(g, art, scale, external);
        paintScale(g, art);
        paintCaption(g, bounds, external, delta);
    }

private:
    void buildArt()
    {
        // Original line-art face, built from mirrored curves (design space 200 x 250).
        const auto mirrorX = [](float x) { return 200.0f - x; };

        head.startNewSubPath(100.0f, 16.0f);
        head.cubicTo(150.0f, 16.0f, 172.0f, 58.0f, 170.0f, 104.0f);
        head.cubicTo(169.0f, 150.0f, 152.0f, 196.0f, 120.0f, 220.0f);
        head.quadraticTo(100.0f, 233.0f, 80.0f, 220.0f);
        head.cubicTo(48.0f, 196.0f, 31.0f, 150.0f, 30.0f, 104.0f);
        head.cubicTo(28.0f, 58.0f, 50.0f, 16.0f, 100.0f, 16.0f);
        head.closeSubPath();

        neck.startNewSubPath(78.0f, 222.0f);
        neck.quadraticTo(80.0f, 238.0f, 72.0f, 250.0f);
        neck.startNewSubPath(122.0f, 222.0f);
        neck.quadraticTo(120.0f, 238.0f, 128.0f, 250.0f);

        for (const auto side : { 0, 1 })
        {
            const auto x = [&](float v) { return side == 0 ? v : mirrorX(v); };
            // Brows
            features.startNewSubPath(x(54.0f), 92.0f);
            features.quadraticTo(x(70.0f), 82.0f, x(88.0f), 89.0f);
            // Closed eyes with lashes
            features.startNewSubPath(x(58.0f), 110.0f);
            features.quadraticTo(x(73.0f), 121.0f, x(88.0f), 110.0f);
            for (const auto lash : { 64.0f, 72.0f, 80.0f })
            {
                const auto y = 113.5f + (juce::exactlyEqual(lash, 72.0f) ? 2.5f : 0.5f);
                features.startNewSubPath(x(lash), y);
                features.lineTo(x(lash - (side == 0 ? 2.0f : -2.0f)), y + 5.0f);
            }
            // Cheekbone hint
            features.startNewSubPath(x(48.0f), 138.0f);
            features.quadraticTo(x(54.0f), 152.0f, x(62.0f), 158.0f);
        }
        // Nose
        features.startNewSubPath(98.0f, 112.0f);
        features.quadraticTo(94.0f, 140.0f, 90.0f, 152.0f);
        features.quadraticTo(100.0f, 160.0f, 110.0f, 152.0f);
        // Lips, parted: she is singing
        lips.startNewSubPath(80.0f, 181.0f);
        lips.cubicTo(88.0f, 172.0f, 95.0f, 173.0f, 100.0f, 177.0f);
        lips.cubicTo(105.0f, 173.0f, 112.0f, 172.0f, 120.0f, 181.0f);
        lips.quadraticTo(100.0f, 186.0f, 80.0f, 181.0f);
        lips.closeSubPath();
        lowerLip.startNewSubPath(82.0f, 183.0f);
        lowerLip.quadraticTo(100.0f, 204.0f, 118.0f, 183.0f);
        mouth.addEllipse(91.0f, 181.0f, 18.0f, 8.0f);

        // Mask: a full-face plate with almond eye holes (beat-coloured)
        mask.startNewSubPath(100.0f, 30.0f);
        mask.cubicTo(146.0f, 30.0f, 164.0f, 66.0f, 163.0f, 104.0f);
        mask.cubicTo(162.0f, 146.0f, 146.0f, 186.0f, 118.0f, 208.0f);
        mask.quadraticTo(100.0f, 219.0f, 82.0f, 208.0f);
        mask.cubicTo(54.0f, 186.0f, 38.0f, 146.0f, 37.0f, 104.0f);
        mask.cubicTo(36.0f, 66.0f, 54.0f, 30.0f, 100.0f, 30.0f);
        mask.closeSubPath();
        for (const auto side : { 0, 1 })
        {
            const auto x = [&](float v) { return side == 0 ? v : mirrorX(v); };
            juce::Path hole;
            hole.startNewSubPath(x(54.0f), 112.0f);
            hole.quadraticTo(x(72.0f), 96.0f, x(92.0f), 110.0f);
            hole.quadraticTo(x(72.0f), 126.0f, x(54.0f), 112.0f);
            hole.closeSubPath();
            mask.addPath(hole);
        }
        mask.setUsingNonZeroWinding(false);

        // Engraving lines across the mask (a nod to spectrum lines)
        for (int line = 0; line < 9; ++line)
        {
            const auto y = 48.0f + static_cast<float>(line) * 18.0f;
            engraving.startNewSubPath(30.0f, y);
            engraving.cubicTo(80.0f, y - 6.0f, 120.0f, y + 6.0f, 170.0f, y);
        }

        // Shards: eight jagged bands (top = air, bottom = lows), each split by a crack down the middle.
        // Equal rows: the face is a log frequency axis, so each 1.25-octave slice gets the same height.
        std::array<float, faceRegions + 1> rows {};
        for (std::size_t row = 0; row <= faceRegions; ++row)
            rows[row] = rowTop + static_cast<float>(row) * rowHeight;
        juce::Random random(7);
        std::array<std::array<float, 9>, faceRegions + 1> jag {};
        for (auto& row : jag)
            for (auto& v : row)
                v = random.nextFloat() * 8.0f - 4.0f;
        for (std::size_t row = 0; row <= faceRegions; ++row)
            jag[row][0] = jag[row][8] = 0.0f;
        std::array<float, faceRegions> crack {};
        for (auto& c : crack)
            c = random.nextFloat() * 10.0f - 5.0f;

        for (std::size_t r = 0; r < faceRegions; ++r)
        {
            for (const auto side : { 0, 1 })
            {
                juce::Path shard;
                const auto edgeY = [&](std::size_t row, int i) { return rows[row] + jag[row][static_cast<std::size_t>(i)]; };
                const auto crackX = 100.0f + crack[r];
                // top edge, left to right across this half
                const auto x0 = side == 0 ? 20.0f : crackX;
                const auto x1 = side == 0 ? crackX : 180.0f;
                const auto i0 = side == 0 ? 0 : 4, i1 = side == 0 ? 4 : 8;
                shard.startNewSubPath(x0, edgeY(r, i0));
                for (int i = i0 + 1; i <= i1; ++i)
                    shard.lineTo(juce::jmap(static_cast<float>(i - i0), 0.0f, 4.0f, x0, x1), edgeY(r, i));
                shard.lineTo(x1, edgeY(r + 1, i1));
                for (int i = i1 - 1; i >= i0; --i)
                    shard.lineTo(juce::jmap(static_cast<float>(i - i0), 0.0f, 4.0f, x0, x1), edgeY(r + 1, i));
                shard.closeSubPath();
                shards[r * 2 + static_cast<std::size_t>(side)] = shard;
                shardDrift[r * 2 + static_cast<std::size_t>(side)] = {
                    (side == 0 ? -1.0f : 1.0f) * (30.0f + random.nextFloat() * 26.0f),
                    -26.0f - random.nextFloat() * 30.0f,
                    (side == 0 ? -1.0f : 1.0f) * (0.18f + random.nextFloat() * 0.25f) };
            }
        }
    }

    void paintFace(juce::Graphics& g, const juce::AffineTransform& art, float scale, float brightness) const
    {
        const auto glow = palette::key.withAlpha(0.22f * brightness);
        const auto line = palette::key.withAlpha(brightness);
        const auto wide = juce::PathStrokeType(5.0f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const auto thin = juce::PathStrokeType(1.7f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        g.setColour(palette::key.withAlpha(0.05f * brightness));
        g.fillPath(head, art);
        for (const auto* path : { &head, &neck, &features, &lowerLip })
        {
            g.setColour(glow);
            g.strokePath(*path, wide, art);
            g.setColour(line);
            g.strokePath(*path, thin, art);
        }
        g.setColour(palette::key.withAlpha(0.35f * brightness));
        g.fillPath(lips, art);
        g.setColour(line);
        g.strokePath(lips, thin, art);
        g.setColour(palette::background);
        g.fillPath(mouth, art);
    }

    void paintMask(juce::Graphics& g, const juce::AffineTransform& art, float scale, bool external) const
    {
        for (std::size_t s = 0; s < shards.size(); ++s)
        {
            // A shard's lift is exactly its range's share of masking removed (linear); it stays solid
            // while it moves and fades only over the last third. Inactive ranges sit in place, faint.
            const auto activity = external ? hub.regionActivity[s / 2] : 1.0f;
            const auto presence = external ? juce::jlimit(0.0f, 1.0f, hub.regionPresence[s / 2]) : 1.0f;
            const auto progress = (1.0f - presence) * activity;
            const auto lift = progress * progress * (3.0f - 2.0f * progress);
            const auto alpha = juce::jlimit(0.0f, 1.0f, (1.0f - progress) / 0.35f)
                               * (external ? 0.22f + 0.78f * activity : 0.55f);
            if (alpha < 0.02f)
                continue;

            const auto& drift = shardDrift[s];
            const auto centre = shards[s].getBounds().getCentre();
            const auto move = juce::AffineTransform::rotation(drift.z * lift, centre.x, centre.y)
                                  .translated(drift.x * lift, drift.y * lift)
                                  .followedBy(art);

            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(shards[s], move);
            g.reduceClipRegion(mask, move);
            const auto top = juce::Point<float>(100.0f, 30.0f).transformedBy(move);
            const auto bottom = juce::Point<float>(100.0f, 215.0f).transformedBy(move);
            g.setGradientFill(juce::ColourGradient(palette::track.brighter(0.25f).withAlpha(alpha), top,
                                                   palette::track.darker(0.55f).withAlpha(alpha), bottom, false));
            g.fillPath(mask, move);
            g.setColour(juce::Colours::black.withAlpha(0.18f * alpha));
            g.strokePath(engraving, juce::PathStrokeType(1.0f * scale), move);
            g.setColour(palette::background.withAlpha(0.85f * alpha));
            g.strokePath(shards[s], juce::PathStrokeType(1.6f * scale), move);
            g.setColour(palette::track.brighter(0.6f).withAlpha(0.5f * alpha));
            g.strokePath(mask, juce::PathStrokeType(1.0f * scale), move);
        }
    }

    void paintScale(juce::Graphics& g, const juce::AffineTransform& art) const
    {
        // The frequency axis the shards follow: boundaries of the eight slices, top = 20 kHz.
        g.setFont(font(10.5f, Weight::semibold));
        for (std::size_t edge = 0; edge <= faceRegions; ++edge)
        {
            const auto y = rowTop + static_cast<float>(edge) * rowHeight;
            const auto a = juce::Point<float>(176.0f, y).transformedBy(art), b = juce::Point<float>(181.0f, y).transformedBy(art);
            g.setColour(palette::dim);
            g.drawLine(a.x, a.y, b.x, b.y, 1.0f);
            g.setColour(palette::muted);
            g.drawText(edgeLabel(1.0f - static_cast<float>(edge) / faceRegions),
                       juce::Rectangle<float>(b.x + 3.0f, b.y - 7.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
        }
    }

    void paintCaption(juce::Graphics& g, juce::Rectangle<float> bounds, bool external, bool delta) const
    {
        auto area = bounds.reduced(16.0f, 0.0f).removeFromBottom(54.0f);
        if (delta)
        {
            g.setColour(palette::collision.withAlpha(0.6f + 0.4f * hub.pulse));
            g.setFont(font(15.0f, Weight::bold));
            g.drawText("DELTA ON", area.removeFromTop(26.0f), juce::Justification::centred);
            g.setColour(palette::muted);
            g.setFont(font(13.0f));
            g.drawText("Hearing only what Carve removes", area.removeFromTop(18.0f), juce::Justification::centred);
            return;
        }
        if (!external)
        {
            g.setColour(palette::muted);
            g.setFont(font(14.0f, Weight::semibold));
            g.drawFittedText("Route a sidechain to\nunmask the " + word(), area.toNearestInt(), juce::Justification::centred, 2);
            return;
        }
        const auto percent = juce::roundToInt(100.0f * hub.clarityShown);
        auto row = area.removeFromTop(30.0f);
        g.setColour(palette::text);
        g.setFont(font(28.0f, Weight::bold));
        g.drawText(juce::String(percent) + "%", row.removeFromLeft(row.getWidth() * 0.4f), juce::Justification::centredRight);
        g.setColour(palette::muted);
        g.setFont(font(13.0f, Weight::semibold));
        g.drawText(juce::String("  ") + sourceNames[static_cast<std::size_t>(sourceIndex(params))] + " CLARITY", row,
                   juce::Justification::centredLeft);
        g.setFont(font(13.0f));
        if (hub.maxCut < -0.25f)
        {
            g.setColour(palette::collision);
            g.drawText("Peak cut " + juce::String(hub.maxCut, 1) + " dB at " + frequencyText(hub.maxCutFrequency, 1),
                       area.removeFromTop(18.0f), juce::Justification::centred);
        }
        else
        {
            g.drawText(hub.vocalPresent ? "No cut needed right now" : "Waiting for the " + word(),
                       area.removeFromTop(18.0f), juce::Justification::centred);
        }
    }

    void showSourceMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader("Making room for");
        for (int index = 0; index < 7; ++index)
            menu.addItem(index + 1, juce::String(sourceNames[static_cast<std::size_t>(index)])
                                    + "   (" + sourceHints[static_cast<std::size_t>(index)] + ")",
                         true, index == sourceIndex(params));
        juce::Component::SafePointer<UnmaskFace> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&source), [safe](int result)
        {
            if (safe != nullptr && result > 0)
            {
                safe->params.state.setProperty("sourceName", result - 1, nullptr);
                safe->refreshSource();
            }
        });
    }

    juce::String word() const { return sourceWords[static_cast<std::size_t>(sourceIndex(params))]; }

    MeterHub& hub;
    juce::AudioProcessorValueTreeState& params;
    juce::TextButton source;
    juce::Path head, neck, features, lips, lowerLip, mouth, mask, engraving;
    std::array<juce::Path, faceRegions * 2> shards;
    struct Drift { float x = 0.0f, y = 0.0f, z = 0.0f; };
    std::array<Drift, faceRegions * 2> shardDrift;
};

//==============================================================================
// Pill toggle bound to a bool parameter.
class PillToggle final : public juce::Button
{
public:
    PillToggle(const juce::String& offText, juce::Colour onColour, juce::RangedAudioParameter& parameter,
               const juce::String& onText = {})
        : juce::Button(offText), offLabel(offText), onLabel(onText.isEmpty() ? offText : onText), colour(onColour),
          attachment(parameter, [this](float v) { setToggleState(v > 0.5f, juce::dontSendNotification); repaint(); })
    {
        setClickingTogglesState(false);
        onClick = [this] { attachment.setValueAsCompleteGesture(getToggleState() ? 0.0f : 1.0f); };
        attachment.sendInitialUpdate();
    }

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        paintPill(g, getLocalBounds().toFloat(), getToggleState(), over, colour, getToggleState() ? onLabel : offLabel);
    }

    static void paintPill(juce::Graphics& g, juce::Rectangle<float> area, bool on, bool over, juce::Colour colour,
                          const juce::String& label)
    {
        area = area.reduced(0.5f);
        const auto radius = area.getHeight() * 0.5f;
        g.setColour(on ? colour.withAlpha(0.16f) : palette::plot);
        g.fillRoundedRectangle(area, radius);
        g.setColour(on ? colour : (over ? palette::dim : palette::edge));
        g.drawRoundedRectangle(area, radius, 1.0f);
        g.setColour(on ? colour : palette::muted);
        g.fillEllipse(area.getX() + 11.0f, area.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour(on ? palette::text : palette::muted);
        g.setFont(font(14.0f, Weight::semibold));
        g.drawText(label, area.withTrimmedLeft(22.0f).withTrimmedRight(8.0f), juce::Justification::centred);
    }

private:
    juce::String offLabel, onLabel;
    juce::Colour colour;
    juce::ParameterAttachment attachment;
};

//==============================================================================
class ModeSwitch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ModeSwitch(juce::RangedAudioParameter& parameter)
        : attachment(parameter, [this](float v) { selected = v > 0.5f ? 1 : 0; repaint(); })
    {
        setTitle("Mode");
        setTooltip("COLLISION measures both tracks and cuts only where they actually overlap.\n"
                   "CLASSIC cuts wherever the sidechain is loud.");
        attachment.sendInitialUpdate();
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(palette::plot);
        g.fillRoundedRectangle(area, 7.0f);
        g.setColour(palette::edge);
        g.drawRoundedRectangle(area, 7.0f, 1.0f);

        const std::array<const char*, 2> names { "COLLISION", "CLASSIC" };
        for (int index = 0; index < 2; ++index)
        {
            const auto cell = segment(index);
            if (index == selected)
            {
                g.setColour(index == 0 ? palette::collision.withAlpha(0.18f) : palette::edge);
                g.fillRoundedRectangle(cell.reduced(3.0f), 5.0f);
                g.setColour(index == 0 ? palette::collision : palette::muted);
                g.drawRoundedRectangle(cell.reduced(3.0f), 5.0f, 1.0f);
            }
            g.setColour(index == selected ? palette::text : palette::muted);
            g.setFont(font(15.0f, Weight::bold));
            g.drawText(names[static_cast<std::size_t>(index)], cell, juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto index = segment(0).contains(event.position) ? 0 : 1;
        if (index != selected)
            attachment.setValueAsCompleteGesture(static_cast<float>(index));
    }

private:
    juce::Rectangle<float> segment(int index) const
    {
        auto area = getLocalBounds().toFloat();
        return index == 0 ? area.removeFromLeft(area.getWidth() * 0.55f) : area.withTrimmedLeft(area.getWidth() * 0.55f);
    }

    juce::ParameterAttachment attachment;
    int selected = 0;
};

//==============================================================================
// Both spectra, the collision zones between them, and the live cut.
class CollisionDisplay final : public juce::Component
{
public:
    CollisionDisplay(EpsilonCarveAudioProcessor& audioProcessor, MeterHub& meterHub)
        : processor(audioProcessor), hub(meterHub),
          lowParameter(*processor.params.getParameter("low")),
          highParameter(*processor.params.getParameter("high"))
    {
        setOpaque(true);
        setTitle("Collision display: track spectrum, sidechain spectrum and applied cut");
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(palette::background);
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(palette::panel);
        g.fillRoundedRectangle(bounds, 12.0f);
        const auto delta = plainValue(processor.params, "delta") > 0.5f;
        g.setColour(delta ? palette::collision.withAlpha(0.4f + 0.6f * hub.pulse) : palette::edge);
        g.drawRoundedRectangle(bounds.reduced(delta ? 1.0f : 0.5f), 12.0f, delta ? 2.0f : 1.0f);

        shownLow = plainValue(processor.params, "low");
        shownHigh = plainValue(processor.params, "high");
        paintPlots(g);
        paintHeader(g);
        paintReadout(g);
        if (delta)
            paintDeltaBanner(g);
    }

    void resized() override
    {
        auto area = getLocalBounds().toFloat().reduced(14.0f, 0.0f);
        area.removeFromTop(40.0f);
        area.removeFromBottom(28.0f);
        area.removeFromLeft(34.0f);
        auto top = area.removeFromTop(area.getHeight() * 0.66f);
        top.removeFromBottom(16.0f);
        spectrum = top;
        area.removeFromBottom(16.0f);
        cut = area;
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        const auto region = spectrum.getUnion(cut);
        hoverPosition = region.contains(event.position) ? (event.position.x - region.getX()) / region.getWidth() : -1.0f;
        setMouseCursor(handleNear(event.position.x) != nullptr && region.contains(event.position)
                           ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoverPosition = -1.0f;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        dragging = spectrum.getUnion(cut).contains(event.position) ? handleNear(event.position.x) : nullptr;
        if (dragging != nullptr)
            dragging->beginChangeGesture();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragging == nullptr)
            return;
        const auto position = juce::jlimit(0.0f, 1.0f, (event.position.x - spectrum.getX()) / spectrum.getWidth());
        dragging->setValueNotifyingHost(dragging->convertTo0to1(carve::frequencyAt(position)));
        hoverPosition = position;
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging != nullptr)
            dragging->endChangeGesture();
        dragging = nullptr;
    }

private:
    float xAt(float position) const { return spectrum.getX() + spectrum.getWidth() * position; }
    float ySpectrum(float db) const
    {
        return spectrum.getY() + spectrum.getHeight()
               * juce::jlimit(0.0f, 1.0f, (spectrumTopDb - db) / (spectrumTopDb - spectrumFloorDb));
    }
    float yCut(float db) const { return cut.getY() + cut.getHeight() * juce::jlimit(0.0f, 1.0f, db / cutFloorDb); }

    juce::RangedAudioParameter* handleNear(float x)
    {
        const auto lowX = xAt(carve::frequencyPosition(plainValue(processor.params, "low")));
        const auto highX = xAt(carve::frequencyPosition(plainValue(processor.params, "high")));
        const auto lowDistance = std::abs(x - lowX), highDistance = std::abs(x - highX);
        if (juce::jmin(lowDistance, highDistance) > 10.0f)
            return nullptr;
        return lowDistance <= highDistance ? &lowParameter : &highParameter;
    }

    juce::Path tracePath(const std::array<float, tracePoints>& values, bool spectrumPlot) const
    {
        juce::Path path;
        for (int index = 0; index < tracePoints; ++index)
        {
            const auto x = xAt(static_cast<float>(index) / static_cast<float>(tracePoints - 1));
            const auto value = values[static_cast<std::size_t>(index)];
            const auto y = spectrumPlot ? ySpectrum(value) : yCut(value);
            index == 0 ? path.startNewSubPath(x, y) : path.lineTo(x, y);
        }
        return path;
    }

    void paintPlots(juce::Graphics& g)
    {
        g.setColour(palette::plot);
        g.fillRoundedRectangle(spectrum.expanded(1.0f), 4.0f);
        g.fillRoundedRectangle(cut.expanded(1.0f), 4.0f);

        g.setFont(font(12.5f));
        const auto label = [&g](float y, const juce::String& t, float x)
        {
            g.setColour(palette::muted);
            g.drawText(t, juce::Rectangle<float>(x - 40.0f, y - 7.0f, 34.0f, 14.0f), juce::Justification::centredRight);
        };
        for (int db = 0; db >= -60; db -= 20)
        {
            const auto y = ySpectrum(static_cast<float>(db));
            g.setColour(palette::edge.withAlpha(0.7f));
            g.drawHorizontalLine(juce::roundToInt(y), spectrum.getX(), spectrum.getRight());
            label(y, juce::String(db), spectrum.getX());
        }
        for (int db = 0; db >= -18; db -= 6)
        {
            const auto y = yCut(static_cast<float>(db));
            g.setColour(palette::edge.withAlpha(db == 0 ? 1.0f : 0.6f));
            g.drawHorizontalLine(juce::roundToInt(y), cut.getX(), cut.getRight());
            label(y, juce::String(db), cut.getX());
        }
        constexpr std::array<float, 10> marks { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
                                              1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
        constexpr std::array<const char*, 10> names { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
        for (std::size_t index = 0; index < marks.size(); ++index)
        {
            const auto x = xAt(carve::frequencyPosition(marks[index]));
            g.setColour(palette::edge.withAlpha(0.45f));
            g.drawVerticalLine(juce::roundToInt(x), spectrum.getY(), spectrum.getBottom());
            g.drawVerticalLine(juce::roundToInt(x), cut.getY(), cut.getBottom());
            g.setColour(palette::muted);
            g.drawText(names[index], juce::Rectangle<float>(x - 20.0f, cut.getBottom() + 3.0f, 40.0f, 14.0f),
                       juce::Justification::centred);
        }

        // Collision zones: coral light rising from the floor where Carve is cutting
        for (int index = 0; index < tracePoints; ++index)
        {
            const auto depth = -hub.cutDb[static_cast<std::size_t>(index)];
            if (depth < 0.2f)
                continue;
            const auto strength = juce::jlimit(0.0f, 1.0f, depth / 6.0f);
            const auto x0 = xAt(static_cast<float>(index) / static_cast<float>(tracePoints - 1));
            const auto x1 = xAt(static_cast<float>(index + 1) / static_cast<float>(tracePoints - 1));
            const auto top = juce::jmin(ySpectrum(hub.trackDb[static_cast<std::size_t>(index)]),
                                        ySpectrum(hub.keyDb[static_cast<std::size_t>(index)]));
            g.setGradientFill(juce::ColourGradient(palette::collision.withAlpha(0.85f * strength), 0.0f, top,
                                                   palette::collision.withAlpha(0.10f * strength), 0.0f,
                                                   spectrum.getBottom(), false));
            g.fillRect(juce::Rectangle<float>(x0, top, x1 - x0 + 0.6f, spectrum.getBottom() - top));
        }

        if (hub.hasFrame)
        {
            auto trackLine = tracePath(hub.trackDb, true);
            auto trackFill = trackLine;
            trackFill.lineTo(spectrum.getRight(), spectrum.getBottom());
            trackFill.lineTo(spectrum.getX(), spectrum.getBottom());
            trackFill.closeSubPath();
            g.setGradientFill(juce::ColourGradient(palette::track.withAlpha(0.22f), 0.0f, spectrum.getY(),
                                                   palette::track.withAlpha(0.02f), 0.0f, spectrum.getBottom(), false));
            g.fillPath(trackFill);
            g.setColour(palette::track.withAlpha(0.9f));
            g.strokePath(trackLine, juce::PathStrokeType(1.4f));

            auto keyLine = tracePath(hub.keyDb, true);
            g.setColour(palette::key.withAlpha(0.22f));
            g.strokePath(keyLine, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(palette::key);
            g.strokePath(keyLine, juce::PathStrokeType(1.7f));
        }

        // Applied cut: glowing coral body under a white edge
        auto cutPath = tracePath(hub.cutDb, false);
        auto cutFill = cutPath;
        cutFill.lineTo(cut.getRight(), yCut(0.0f));
        cutFill.lineTo(cut.getX(), yCut(0.0f));
        cutFill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(palette::collision.withAlpha(0.25f), 0.0f, cut.getY(),
                                               palette::collision.withAlpha(0.85f), 0.0f, yCut(-9.0f), false));
        g.fillPath(cutFill);
        g.setColour(palette::collision.withAlpha(0.35f));
        g.strokePath(cutPath, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(palette::text);
        g.strokePath(cutPath, juce::PathStrokeType(1.7f));
        if (hub.hasFrame && hub.frame.widthGuard)
        {
            juce::Path dashed;
            const std::array<float, 2> dashes { 5.0f, 4.0f };
            juce::PathStrokeType(1.3f).createDashedStroke(dashed, tracePath(hub.sideCutDb, false), dashes.data(), 2);
            g.setColour(palette::key.withAlpha(0.85f));
            g.fillPath(dashed);
        }

        // Outside the cutoffs
        const auto left = xAt(carve::frequencyPosition(shownLow));
        const auto right = xAt(carve::frequencyPosition(shownHigh));
        for (const auto& area : { spectrum, cut })
        {
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            if (left < right)
            {
                g.fillRect(area.withRight(juce::jmax(area.getX(), left)));
                g.fillRect(area.withLeft(juce::jmin(area.getRight(), right)));
            }
            else
            {
                g.fillRect(area);
            }
        }
        for (const auto x : { left, right })
        {
            if (x <= spectrum.getX() + 0.5f || x >= spectrum.getRight() - 0.5f)
                continue;
            g.setColour(palette::text.withAlpha(0.55f));
            g.drawLine(x, spectrum.getY(), x, cut.getBottom(), 1.0f);
            const auto grip = juce::Rectangle<float>(7.0f, 24.0f).withCentre({ x, spectrum.getY() + 17.0f });
            g.setColour(palette::text);
            g.fillRoundedRectangle(grip, 3.5f);
        }

        if (hoverPosition >= 0.0f)
        {
            g.setColour(palette::muted.withAlpha(0.6f));
            g.drawVerticalLine(juce::roundToInt(xAt(hoverPosition)), spectrum.getY(), cut.getBottom());
        }

        g.setColour(palette::muted);
        g.setFont(font(12.5f, Weight::semibold));
        g.drawText("CUT", juce::Rectangle<float>(cut.getX() + 8.0f, cut.getY() + 3.0f, 60.0f, 14.0f),
                   juce::Justification::centredLeft);
    }

    juce::String statusText(juce::Colour& colour) const
    {
        colour = palette::muted;
        if (!hub.hasFrame)
            return "WAITING FOR AUDIO";
        if (hub.frame.source == carve::TriggerSource::self)
            return "INTERNAL SIDECHAIN";
        if (hub.frame.source == carve::TriggerSource::unavailable)
            return "NO SIDECHAIN ROUTED";
        if (hub.frame.triggerInputPeak < 0.0001f)
            return "SIDECHAIN QUIET";
        colour = palette::key;
        return "SIDECHAIN LIVE";
    }

    void paintHeader(juce::Graphics& g) const
    {
        const auto row = juce::Rectangle<float>(14.0f, 10.0f, static_cast<float>(getWidth()) - 28.0f, 22.0f);
        auto x = row.getX();
        const auto legend = [&g, &x, &row](juce::Colour colour, const juce::String& t, float width)
        {
            g.setColour(colour);
            g.fillRoundedRectangle(x, row.getCentreY() - 2.0f, 14.0f, 4.0f, 2.0f);
            g.setColour(palette::text);
            g.setFont(font(14.0f, Weight::semibold));
            g.drawText(t, juce::Rectangle<float>(x + 20.0f, row.getY(), width, row.getHeight()),
                       juce::Justification::centredLeft);
            x += width + 24.0f;
        };
        legend(palette::track, "TRACK", 42.0f);
        legend(palette::key, "SIDECHAIN", 70.0f);
        legend(palette::collision, "COLLISION", 70.0f);

        juce::Colour colour;
        const auto status = statusText(colour);
        const auto badge = juce::Rectangle<float>(row.getRight() - 176.0f, row.getY(), 176.0f, row.getHeight());
        g.setColour(colour.withAlpha(0.12f));
        g.fillRoundedRectangle(badge, 5.0f);
        g.setColour(colour);
        g.fillEllipse(badge.getX() + 10.0f, badge.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setFont(font(13.5f, Weight::bold));
        g.drawText(status, badge.withTrimmedLeft(22.0f), juce::Justification::centredLeft);
    }

    void paintReadout(juce::Graphics& g) const
    {
        juce::String message = "Drag the white handles to set the range. Hover to read both tracks and the cut.";
        if (shownLow >= shownHigh)
        {
            message = "LOW CUT IS ABOVE HIGH CUT  /  NOTHING IS BEING CARVED";
        }
        else if (hub.hasFrame && hoverPosition >= 0.0f)
        {
            const auto index = static_cast<std::size_t>(juce::jlimit(0, tracePoints - 1,
                                                                     juce::roundToInt(hoverPosition * (tracePoints - 1))));
            message = frequencyText(carve::frequencyAt(hoverPosition), 2)
                + "     Track " + juce::String(hub.trackDb[index], 1) + " dB"
                + "     Sidechain " + juce::String(hub.keyDb[index], 1) + " dB"
                + "     Cut " + juce::String(hub.cutDb[index], 1) + " dB";
        }
        g.setColour(palette::muted);
        g.setFont(font(13.5f));
        g.drawFittedText(message, 14, getHeight() - 23, getWidth() - 28, 16, juce::Justification::centredLeft, 1);
    }

    void paintDeltaBanner(juce::Graphics& g) const
    {
        const auto banner = spectrum.withHeight(28.0f).withSizeKeepingCentre(320.0f, 28.0f).translated(0.0f, 30.0f);
        g.setColour(palette::collision.withAlpha(0.88f));
        g.fillRoundedRectangle(banner, 6.0f);
        g.setColour(palette::background);
        g.setFont(font(15.0f, Weight::bold));
        g.drawText("DELTA: YOU ARE HEARING ONLY THE CUTS", banner, juce::Justification::centred);
    }

    EpsilonCarveAudioProcessor& processor;
    MeterHub& hub;
    juce::RangedAudioParameter& lowParameter;
    juce::RangedAudioParameter& highParameter;
    juce::RangedAudioParameter* dragging = nullptr;
    juce::Rectangle<float> spectrum, cut;
    float shownLow = carve::lowestBandHz, shownHigh = carve::highestBandHz;
    float hoverPosition = -1.0f;
};

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
        for (auto* c : std::initializer_list<juce::Component*> { &display, &face, &mode, &guard, &delta, &sidechain, &advanced })
            addAndMakeVisible(c);
        guard.setTooltip("Carve only the centre (Mid) of the track. The stereo sides keep their width.");
        delta.setTooltip("Hear only what Carve removes. Turn it off before you bounce.");
        sidechain.setTooltip("EXTERNAL: the routed sidechain (your vocal) drives the cuts.\n"
                             "INTERNAL: the track carves itself, like a 32-band dynamic EQ.");
        advanced.setTooltip("Show or hide Sensitivity, Attack, Release, Mix, Width Guard and Delta.");
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
        const auto id = processor.getCurrentProgram() + 1;
        if (presetBox.getSelectedId() != id)
            presetBox.setSelectedId(id, juce::dontSendNotification);
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
        g.drawText("v0.5.1", 156, 22, 60, 18, juce::Justification::centredLeft);

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
        advanced.setBounds(width / 2 - 70, toggleY, 140, 30);
        guard.setBounds(width - 22 - 150 - 10 - 150, toggleY, 150, 30);
        delta.setBounds(width - 22 - 150, toggleY, 150, 30);
        layoutKnobs();
    }

private:
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
        sizeLimits->setSizeLimits(designWidth * 3 / 4, designHeight * 3 / 4, designWidth * 2, designHeight * 2);
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
