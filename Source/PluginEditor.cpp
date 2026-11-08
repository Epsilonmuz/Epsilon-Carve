#include "PluginEditor.h"

#include <cmath>

namespace
{
namespace palette
{
const juce::Colour background { 0xff0a0c10 };
const juce::Colour panel      { 0xff11151c };
const juce::Colour plot       { 0xff0c0f14 };
const juce::Colour edge       { 0xff222a36 };
const juce::Colour text       { 0xffeef2f7 };
const juce::Colour muted      { 0xff8d99ab };
const juce::Colour dim        { 0xff4a5568 };
const juce::Colour track      { 0xfff2a541 };   // the signal being carved
const juce::Colour key        { 0xff3fd0ff };   // the sidechain
const juce::Colour collision  { 0xffff4d6d };   // where they collide and Carve cuts
}

constexpr float spectrumFloorDb = -72.0f;
constexpr float cutFloorDb = -18.0f;
constexpr int tracePoints = 200;

juce::Font font(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::String frequencyText(float frequency, int decimals)
{
    return frequency < 1000.0f ? juce::String(frequency, 0) + " Hz"
                               : juce::String(frequency * 0.001f, decimals) + " kHz";
}

float plainValue(const juce::AudioProcessorValueTreeState& state, const char* id)
{
    return state.getRawParameterValue(id)->load(std::memory_order_relaxed);
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
        const auto hero = slider.getProperties()["hero"];
        const auto angle = start + value * (end - start);
        const auto ring = radius - 2.0f;
        const auto thickness = hero ? 5.0f : 3.0f;

        // Value ring
        juce::Path rail, active;
        rail.addCentredArc(centre.x, centre.y, ring, ring, 0.0f, start, end, true);
        active.addCentredArc(centre.x, centre.y, ring, ring, 0.0f, start, angle, true);
        const juce::PathStrokeType stroke(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour(palette::edge);
        g.strokePath(rail, stroke);
        if (hero)
        {
            g.setColour(accent.withAlpha(0.22f));
            g.strokePath(active, juce::PathStrokeType(thickness + 6.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }
        g.setColour(accent);
        g.strokePath(active, stroke);

        // Body: a machined puck with a chiselled facet across the top
        const auto bodyRadius = ring - thickness - (hero ? 7.0f : 5.0f);
        const auto body = juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(body.translated(0.0f, 2.5f).expanded(1.0f));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a313d), body.getX(), body.getY(),
                                               juce::Colour(0xff12161d), body.getRight(), body.getBottom(), false));
        g.fillEllipse(body);
        juce::Path facet;
        facet.addCentredArc(centre.x, centre.y, bodyRadius - 1.0f, bodyRadius - 1.0f, 0.0f,
                            -2.2f, 0.6f, true);
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.strokePath(facet, juce::PathStrokeType(1.4f));
        g.setColour(palette::edge.brighter(0.25f));
        g.drawEllipse(body, 1.0f);

        // Pointer
        const auto inner = centre.getPointOnCircumference(bodyRadius * 0.30f, angle);
        const auto outer = centre.getPointOnCircumference(bodyRadius * 0.86f, angle);
        g.setColour(slider.isMouseOverOrDragging() ? palette::text : palette::text.withAlpha(0.9f));
        g.drawLine(juce::Line<float>(inner, outer), hero ? 3.0f : 2.2f);
    }

    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* label = LookAndFeel_V4::createSliderTextBox(slider);
        label->setFont(font(slider.getProperties()["hero"] ? 17.0f : 13.5f, true));
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
        label.setFont(font(14.5f, true));
        label.setJustificationType(juce::Justification::centred);
    }

    juce::Font getPopupMenuFont() override { return font(14.0f); }
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
        const auto on = getToggleState();
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        const auto radius = area.getHeight() * 0.5f;
        g.setColour(on ? colour.withAlpha(0.16f) : palette::plot);
        g.fillRoundedRectangle(area, radius);
        g.setColour(on ? colour : (over ? palette::dim : palette::edge));
        g.drawRoundedRectangle(area, radius, 1.0f);
        g.setColour(on ? colour : palette::muted);
        g.fillEllipse(area.getX() + 10.0f, area.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour(on ? palette::text : palette::muted);
        g.setFont(font(12.0f, true));
        g.drawText(on ? onLabel : offLabel, area.withTrimmedLeft(20.0f), juce::Justification::centred);
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
            g.setFont(font(12.5f, true));
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
// The hero: both spectra, the collision zones between them, and the live cut.
class CollisionDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit CollisionDisplay(EpsilonCarveAudioProcessor& audioProcessor)
        : processor(audioProcessor),
          lowParameter(*processor.params.getParameter("low")),
          highParameter(*processor.params.getParameter("high"))
    {
        setOpaque(true);
        setTitle("Collision display: track spectrum, sidechain spectrum and applied cut");
        processor.readMeterFrame(frame);
        processor.setMeteringEnabled(true);
        startTimerHz(30);
    }

    ~CollisionDisplay() override
    {
        stopTimer();
        processor.setMeteringEnabled(false);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(palette::background);
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(palette::panel);
        g.fillRoundedRectangle(bounds, 12.0f);
        g.setColour(palette::edge);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 12.0f, 1.0f);

        computeTraces();
        paintPlots(g);
        paintHeader(g);
        paintReadout(g);
    }

    void resized() override { layout(); }

    void mouseMove(const juce::MouseEvent& event) override
    {
        const auto region = spectrum.getUnion(cut);
        hoverPosition = region.contains(event.position) ? (event.position.x - region.getX()) / region.getWidth() : -1.0f;
        const auto handle = handleNear(event.position.x);
        setMouseCursor(handle != nullptr && region.contains(event.position)
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
    void layout()
    {
        auto area = getLocalBounds().toFloat().reduced(16.0f, 0.0f);
        area.removeFromTop(44.0f);
        area.removeFromBottom(30.0f);
        area.removeFromLeft(40.0f);
        auto top = area.removeFromTop(area.getHeight() * 0.64f);
        top.removeFromBottom(16.0f);
        spectrum = top;
        area.removeFromBottom(16.0f);
        cut = area;
    }

    float xAt(float position) const { return spectrum.getX() + spectrum.getWidth() * position; }
    float ySpectrum(float db) const { return spectrum.getY() + spectrum.getHeight() * juce::jlimit(0.0f, 1.0f, db / spectrumFloorDb); }
    float yCut(float db) const { return cut.getY() + cut.getHeight() * juce::jlimit(0.0f, 1.0f, db / cutFloorDb); }

    juce::RangedAudioParameter* handleNear(float x)
    {
        const auto lowX = xAt(carve::frequencyPosition(plainValue(processor.params, "low")));
        const auto highX = xAt(carve::frequencyPosition(plainValue(processor.params, "high")));
        const auto lowDistance = std::abs(x - lowX), highDistance = std::abs(x - highX);
        if (juce::jmin(lowDistance, highDistance) > 9.0f)
            return nullptr;
        return lowDistance <= highDistance ? &lowParameter : &highParameter;
    }

    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounter();
        auto redraw = false;
        if (processor.readMeterFrame(frame))
        {
            hasFrame = true;
            lastFrameMs = now;
            redraw = true;
        }
        else if (hasFrame && now - lastFrameMs > 300)
        {
            hasFrame = false;
            redraw = true;
        }

        const auto low = plainValue(processor.params, "low"), high = plainValue(processor.params, "high");
        const auto guard = plainValue(processor.params, "guard") > 0.5f;
        if (!juce::exactlyEqual(low, shownLow) || !juce::exactlyEqual(high, shownHigh) || guard != shownGuard)
        {
            shownLow = low;
            shownHigh = high;
            shownGuard = guard;
            redraw = true;
        }
        if (redraw)
            repaint();
    }

    float bandDb(float position, bool targetSide) const
    {
        const auto index = juce::jlimit(0.0f, 1.0f, position) * static_cast<float>(carve::bandCount - 1);
        const auto from = static_cast<std::size_t>(index);
        const auto to = juce::jmin(from + 1, carve::bandCount - 1);
        const auto level = [&](std::size_t i)
        {
            const auto& band = frame.bands[i];
            return juce::Decibels::gainToDecibels(targetSide ? band.targetPeak : band.triggerPeak, spectrumFloorDb);
        };
        return juce::jmap(index - static_cast<float>(from), level(from), level(to));
    }

    void computeTraces()
    {
        maxCut = 0.0f;
        maxCutFrequency = 0.0f;
        for (int index = 0; index < tracePoints; ++index)
        {
            const auto position = static_cast<float>(index) / static_cast<float>(tracePoints - 1);
            const auto frequency = carve::frequencyAt(position);
            traceKey[static_cast<std::size_t>(index)] = hasFrame ? bandDb(position, false) : spectrumFloorDb;
            traceTrack[static_cast<std::size_t>(index)] = hasFrame ? bandDb(position, true) : spectrumFloorDb;
            const auto mid = hasFrame ? carve::cutResponseDb(frame, frequency, false) : 0.0f;
            traceCut[static_cast<std::size_t>(index)] = mid;
            traceSide[static_cast<std::size_t>(index)] = hasFrame && frame.widthGuard
                                                         ? carve::cutResponseDb(frame, frequency, true) : mid;
            if (mid < maxCut)
            {
                maxCut = mid;
                maxCutFrequency = frequency;
            }
        }
    }

    juce::Path tracePath(const std::array<float, tracePoints>& values, bool spectrumPlot) const
    {
        juce::Path path;
        for (int index = 0; index < tracePoints; ++index)
        {
            const auto x = xAt(static_cast<float>(index) / static_cast<float>(tracePoints - 1));
            const auto y = spectrumPlot ? ySpectrum(values[static_cast<std::size_t>(index)])
                                        : yCut(values[static_cast<std::size_t>(index)]);
            index == 0 ? path.startNewSubPath(x, y) : path.lineTo(x, y);
        }
        return path;
    }

    void paintPlots(juce::Graphics& g)
    {
        // Backgrounds and grid
        g.setColour(palette::plot);
        g.fillRoundedRectangle(spectrum.expanded(1.0f), 4.0f);
        g.fillRoundedRectangle(cut.expanded(1.0f), 4.0f);

        g.setFont(font(11.0f));
        const auto label = [&g](float y, const juce::String& text, float x)
        {
            g.setColour(palette::muted);
            g.drawText(text, juce::Rectangle<float>(x - 44.0f, y - 7.0f, 38.0f, 14.0f), juce::Justification::centredRight);
        };
        for (int db = 0; db >= -48; db -= 24)
        {
            const auto y = ySpectrum(static_cast<float>(db));
            g.setColour(palette::edge.withAlpha(0.7f));
            g.drawHorizontalLine(juce::roundToInt(y), spectrum.getX(), spectrum.getRight());
            label(y, db == 0 ? "0 dB" : juce::String(db), spectrum.getX());
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
            g.drawText(names[index], juce::Rectangle<float>(x - 20.0f, cut.getBottom() + 3.0f, 40.0f, 13.0f),
                       juce::Justification::centred);
        }

        // Collision zones: coral columns where Carve is cutting, as tall as the overlap
        for (int index = 0; index < tracePoints; ++index)
        {
            const auto depth = -traceCut[static_cast<std::size_t>(index)];
            if (depth < 0.25f)
                continue;
            const auto strength = juce::jlimit(0.0f, 1.0f, depth / 9.0f);
            const auto x0 = xAt(static_cast<float>(index) / static_cast<float>(tracePoints - 1));
            const auto x1 = xAt(static_cast<float>(index + 1) / static_cast<float>(tracePoints - 1));
            const auto top = juce::jmin(ySpectrum(traceTrack[static_cast<std::size_t>(index)]),
                                        ySpectrum(traceKey[static_cast<std::size_t>(index)]));
            g.setGradientFill(juce::ColourGradient(palette::collision.withAlpha(0.55f * strength), 0.0f, top,
                                                   palette::collision.withAlpha(0.04f * strength), 0.0f,
                                                   spectrum.getBottom(), false));
            g.fillRect(juce::Rectangle<float>(x0, top, x1 - x0 + 0.6f, spectrum.getBottom() - top));
        }

        // Track (amber) and sidechain (cyan) spectra
        if (hasFrame)
        {
            auto track = tracePath(traceTrack, true);
            auto trackFill = track;
            trackFill.lineTo(spectrum.getRight(), spectrum.getBottom());
            trackFill.lineTo(spectrum.getX(), spectrum.getBottom());
            trackFill.closeSubPath();
            g.setGradientFill(juce::ColourGradient(palette::track.withAlpha(0.20f), 0.0f, spectrum.getY(),
                                                   palette::track.withAlpha(0.02f), 0.0f, spectrum.getBottom(), false));
            g.fillPath(trackFill);
            g.setColour(palette::track.withAlpha(0.85f));
            g.strokePath(track, juce::PathStrokeType(1.5f));

            auto key = tracePath(traceKey, true);
            g.setColour(palette::key.withAlpha(0.25f));
            g.strokePath(key, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(palette::key);
            g.strokePath(key, juce::PathStrokeType(1.8f));
        }

        // Applied cut: coral body under a white edge
        auto cutPath = tracePath(traceCut, false);
        auto cutFill = cutPath;
        cutFill.lineTo(cut.getRight(), yCut(0.0f));
        cutFill.lineTo(cut.getX(), yCut(0.0f));
        cutFill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(palette::collision.withAlpha(0.10f), 0.0f, cut.getY(),
                                               palette::collision.withAlpha(0.55f), 0.0f, cut.getBottom(), false));
        g.fillPath(cutFill);
        g.setColour(palette::text);
        g.strokePath(cutPath, juce::PathStrokeType(1.7f));
        if (hasFrame && frame.widthGuard)
        {
            juce::Path dashed;
            const std::array<float, 2> dashes { 5.0f, 4.0f };
            juce::PathStrokeType(1.3f).createDashedStroke(dashed, tracePath(traceSide, false), dashes.data(), 2);
            g.setColour(palette::key.withAlpha(0.8f));
            g.fillPath(dashed);
        }

        // Range outside the cutoffs
        const auto left = xAt(carve::frequencyPosition(shownLow));
        const auto right = xAt(carve::frequencyPosition(shownHigh));
        for (const auto& plot : { spectrum, cut })
        {
            g.setColour(juce::Colours::black.withAlpha(0.45f));
            if (left < right)
            {
                g.fillRect(plot.withRight(juce::jmax(plot.getX(), left)));
                g.fillRect(plot.withLeft(juce::jmin(plot.getRight(), right)));
            }
            else
            {
                g.fillRect(plot);
            }
        }
        for (const auto x : { left, right })
        {
            if (x <= spectrum.getX() + 0.5f || x >= spectrum.getRight() - 0.5f)
                continue;
            g.setColour(palette::text.withAlpha(0.55f));
            g.drawLine(x, spectrum.getY(), x, cut.getBottom(), 1.0f);
            const auto grip = juce::Rectangle<float>(6.0f, 22.0f).withCentre({ x, spectrum.getY() + 16.0f });
            g.setColour(palette::text);
            g.fillRoundedRectangle(grip, 3.0f);
        }

        if (hoverPosition >= 0.0f)
        {
            g.setColour(palette::muted.withAlpha(0.6f));
            g.drawVerticalLine(juce::roundToInt(xAt(hoverPosition)), spectrum.getY(), cut.getBottom());
        }

        g.setColour(palette::muted);
        g.setFont(font(10.5f, true));
        g.drawText("CUT", juce::Rectangle<float>(cut.getX() + 8.0f, cut.getY() + 4.0f, 60.0f, 13.0f),
                   juce::Justification::centredLeft);
    }

    juce::String statusText(juce::Colour& colour) const
    {
        colour = palette::muted;
        if (!hasFrame)
            return "WAITING FOR AUDIO";
        if (frame.source == carve::TriggerSource::self)
            return "SELF MODE  /  CARVING ITSELF";
        if (frame.source == carve::TriggerSource::unavailable)
            return "NO SIDECHAIN ROUTED  /  SELF";
        if (frame.triggerInputPeak < 0.0001f)
            return "SIDECHAIN QUIET";
        colour = palette::key;
        return "SIDECHAIN LIVE";
    }

    void paintHeader(juce::Graphics& g) const
    {
        const auto row = juce::Rectangle<float>(16.0f, 12.0f, static_cast<float>(getWidth()) - 32.0f, 22.0f);
        auto x = row.getX();
        const auto legend = [&g, &x, &row](juce::Colour colour, const juce::String& text, float width)
        {
            g.setColour(colour);
            g.fillRoundedRectangle(x, row.getCentreY() - 2.0f, 14.0f, 4.0f, 2.0f);
            g.setColour(palette::text);
            g.setFont(font(12.0f, true));
            g.drawText(text, juce::Rectangle<float>(x + 20.0f, row.getY(), width, row.getHeight()),
                       juce::Justification::centredLeft);
            x += width + 26.0f;
        };
        legend(palette::track, "TRACK", 48.0f);
        legend(palette::key, "SIDECHAIN", 76.0f);
        legend(palette::collision, "COLLISION", 76.0f);

        juce::Colour colour;
        const auto status = statusText(colour);
        const auto badge = juce::Rectangle<float>(row.getRight() - 214.0f, row.getY(), 214.0f, row.getHeight());
        g.setColour(colour.withAlpha(0.12f));
        g.fillRoundedRectangle(badge, 5.0f);
        g.setColour(colour);
        g.fillEllipse(badge.getX() + 10.0f, badge.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setFont(font(11.5f, true));
        g.drawText(status, badge.withTrimmedLeft(22.0f), juce::Justification::centredLeft);

        if (maxCut < -0.25f)
        {
            g.setColour(palette::collision);
            g.setFont(font(12.0f, true));
            g.drawText("PEAK CUT " + juce::String(maxCut, 1) + " dB @ " + frequencyText(maxCutFrequency, 1),
                       juce::Rectangle<float>(badge.getX() - 232.0f, row.getY(), 222.0f, row.getHeight()),
                       juce::Justification::centredRight);
        }
    }

    void paintReadout(juce::Graphics& g) const
    {
        juce::String message = "Drag the white handles to set the range. Hover to read both tracks and the cut.";
        if (shownLow >= shownHigh)
        {
            message = "LOW CUT IS ABOVE HIGH CUT  /  NOTHING IS BEING CARVED";
        }
        else if (hasFrame && hoverPosition >= 0.0f)
        {
            const auto index = juce::jlimit(0, tracePoints - 1, juce::roundToInt(hoverPosition * (tracePoints - 1)));
            message = frequencyText(carve::frequencyAt(hoverPosition), 2)
                + "     Track " + juce::String(bandDb(hoverPosition, true), 1) + " dB"
                + "     Sidechain " + juce::String(bandDb(hoverPosition, false), 1) + " dB"
                + "     Cut " + juce::String(traceCut[static_cast<std::size_t>(index)], 1) + " dB";
        }
        g.setColour(palette::muted);
        g.setFont(font(12.0f));
        g.drawFittedText(message, 16, getHeight() - 24, getWidth() - 32, 16, juce::Justification::centredLeft, 1);
    }

    EpsilonCarveAudioProcessor& processor;
    juce::RangedAudioParameter& lowParameter;
    juce::RangedAudioParameter& highParameter;
    juce::RangedAudioParameter* dragging = nullptr;
    carve::MeterFrame frame;
    std::array<float, tracePoints> traceKey {}, traceTrack {}, traceCut {}, traceSide {};
    juce::Rectangle<float> spectrum, cut;
    float shownLow = carve::lowestBandHz, shownHigh = carve::highestBandHz;
    bool shownGuard = false;
    float hoverPosition = -1.0f;
    float maxCut = 0.0f, maxCutFrequency = 0.0f;
    bool hasFrame = false;
    juce::uint32 lastFrameMs = 0;
};

//==============================================================================
class EpsilonCarveAudioProcessorEditor::Panel final : public juce::Component
{
public:
    Panel(EpsilonCarveAudioProcessor& p, CarveLook& look)
        : processor(p),
          display(p),
          mode(*p.params.getParameter("mode")),
          guard("WIDTH GUARD", palette::key, *p.params.getParameter("guard")),
          delta("DELTA", palette::collision, *p.params.getParameter("delta"), "DELTA  /  HEARING CUTS"),
          sidechain("SELF MODE", palette::key, *p.params.getParameter("external"), "SIDECHAIN IN")
    {
        setOpaque(true);
        addAndMakeVisible(display);
        addAndMakeVisible(mode);
        addAndMakeVisible(guard);
        addAndMakeVisible(delta);
        addAndMakeVisible(sidechain);
        guard.setTooltip("Carve only the centre (Mid) of the track. The stereo sides keep their width.");
        delta.setTooltip("Hear only what Carve removes. Turn off before you bounce.");
        sidechain.setTooltip("SIDECHAIN IN: the routed sidechain drives the cuts.\nSELF MODE: the track carves itself (Classic behaviour).");

        struct Spec { const char* id; const char* name; double initial; bool hero; juce::Colour colour; const char* tip; };
        const std::array<Spec, 7> specs { {
            { "sensitivity", "SENSITIVITY", 0.0, false, palette::muted, "How strongly the sidechain counts. Raise it for quiet or distant sources." },
            { "attack", "ATTACK", 15.0, false, palette::muted, "How fast the cut arrives when the sidechain comes in." },
            { "low", "LOW CUT", carve::lowestBandHz, false, palette::text.withAlpha(0.75f), "Lowest frequency Carve may cut. Raise it to protect kick and bass." },
            { "amount", "AMOUNT", 25.0, true, palette::collision, "How hard Carve cuts. Start around 25-40%." },
            { "high", "HIGH CUT", carve::highestBandHz, false, palette::text.withAlpha(0.75f), "Highest frequency Carve may cut. Lower it to keep cymbals and air." },
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
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, spec.hero ? 110 : 90, spec.hero ? 24 : 20);
            knob.setScrollWheelEnabled(true);
            knob.setDoubleClickReturnValue(true, spec.initial);
            knob.setColour(juce::Slider::rotarySliderFillColourId, spec.colour);
            knob.setTitle(spec.name);
            knob.setTooltip(spec.tip);
            knob.setPopupDisplayEnabled(false, false, nullptr);
            addAndMakeVisible(knob);
            attachments[index] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params, spec.id, knob);

            auto& label = labels[index];
            label.setText(spec.name, juce::dontSendNotification);
            label.setFont(font(spec.hero ? 15.0f : 11.5f, true));
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
            button->setColour(juce::TextButton::buttonColourId, palette::plot);
            button->setColour(juce::TextButton::textColourOffId, palette::muted);
            button->setColour(juce::ComboBox::outlineColourId, palette::edge);
            addAndMakeVisible(*button);
        }
        previous.onClick = [this] { step(-1); };
        next.onClick = [this] { step(1); };
    }

    ~Panel() override
    {
        for (auto& knob : knobs)
            knob.setLookAndFeel(nullptr);
        presetBox.setLookAndFeel(nullptr);
    }

    void syncPreset()
    {
        const auto id = processor.getCurrentProgram() + 1;
        if (presetBox.getSelectedId() != id)
            presetBox.setSelectedId(id, juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(palette::background);

        // Chiselled corner: the Carve signature
        juce::Path notch;
        notch.startNewSubPath(0.0f, 0.0f);
        notch.lineTo(150.0f, 0.0f);
        notch.lineTo(108.0f, 70.0f);
        notch.lineTo(0.0f, 70.0f);
        notch.closeSubPath();
        g.setGradientFill(juce::ColourGradient(palette::collision.withAlpha(0.20f), 0.0f, 0.0f,
                                               palette::collision.withAlpha(0.0f), 150.0f, 70.0f, false));
        g.fillPath(notch);

        // Wordmark
        g.setColour(palette::text);
        g.setFont(font(13.0f, true));
        g.drawText(juce::CharPointer_UTF8("E\xce\xa3  EPSILON SOUNDS"), 26, 14, 150, 16, juce::Justification::centredLeft);
        g.setColour(palette::dim);
        g.setFont(font(11.0f, true));
        g.drawText("v0.4.0", 170, 14, 60, 16, juce::Justification::centredLeft);
        g.setColour(palette::text);
        g.setFont(font(30.0f, true));
        g.drawText("CARVE", 24, 28, 140, 34, juce::Justification::centredLeft);
        g.setColour(palette::collision);
        g.fillRect(132.0f, 52.0f, 18.0f, 4.0f);
        g.setColour(palette::muted);
        g.setFont(font(11.0f));
        g.drawText("COLLISION-AWARE SIDECHAIN EQ", 160, 41, 200, 16, juce::Justification::centredLeft);

        // Divider above the controls
        g.setColour(palette::edge);
        g.drawHorizontalLine(controlsTop, 22.0f, static_cast<float>(getWidth()) - 22.0f);
    }

    void resized() override
    {
        const auto width = getWidth();
        presetBox.setBounds(416, 22, 214, 32);
        previous.setBounds(presetBox.getX() - 36, 22, 32, 32);
        next.setBounds(presetBox.getRight() + 4, 22, 32, 32);
        mode.setBounds(width - 22 - 210, 22, 210, 32);

        display.setBounds(22, 70, width - 44, 312);
        const auto toggles = juce::Rectangle<int>(22, 390, width - 44, 28);
        sidechain.setBounds(toggles.getX(), toggles.getY(), 164, toggles.getHeight());
        guard.setBounds(toggles.getRight() - 164 - 210 - 10, toggles.getY(), 164, toggles.getHeight());
        delta.setBounds(toggles.getRight() - 210, toggles.getY(), 210, toggles.getHeight());

        controlsTop = 426;
        const std::array<int, 7> sizes { 72, 72, 88, 132, 88, 72, 72 };
        const std::array<float, 7> centres { 0.075f, 0.195f, 0.335f, 0.5f, 0.665f, 0.805f, 0.925f };
        for (std::size_t index = 0; index < knobs.size(); ++index)
        {
            const auto size = sizes[index];
            const auto hero = index == 3;
            const auto centreX = juce::roundToInt(static_cast<float>(width) * centres[index]);
            const auto top = hero ? controlsTop + 6 : controlsTop + 26;
            const auto textHeight = hero ? 24 : 20;
            knobs[index].setBounds(centreX - (size + 20) / 2, top, size + 20, size + textHeight);
            labels[index].setBounds(centreX - 60, knobs[index].getBottom() + (hero ? 0 : 2), 120, 18);
        }
    }

private:
    void step(int direction)
    {
        const auto count = processor.getNumPrograms();
        processor.setCurrentProgram((processor.getCurrentProgram() + direction + count) % count);
        syncPreset();
    }

    EpsilonCarveAudioProcessor& processor;
    CollisionDisplay display;
    ModeSwitch mode;
    PillToggle guard, delta, sidechain;
    std::array<juce::Slider, 7> knobs;
    std::array<juce::Label, 7> labels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 7> attachments;
    juce::ComboBox presetBox;
    juce::TextButton previous { juce::CharPointer_UTF8("\xe2\x97\x80") }, next { juce::CharPointer_UTF8("\xe2\x96\xb6") };
    juce::TooltipWindow tooltips { this, 600 };
    int controlsTop = 426;
};

//==============================================================================
EpsilonCarveAudioProcessorEditor::EpsilonCarveAudioProcessorEditor(EpsilonCarveAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p),
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
