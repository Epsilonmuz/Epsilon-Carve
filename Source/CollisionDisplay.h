#pragma once

// The collision display: both spectra, collision zones, the cut curve and the range handles.

#include "MeterHub.h"

namespace carveui
{
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

        // Collision zones: coral light rising from the floor where Carve is cutting, with a fine
        // diagonal hatch so they read by pattern as well as colour (red-green colour blindness).
        juce::Path zones;
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
            if (depth >= 1.0f)
                zones.addRectangle(x0, top, x1 - x0 + 0.6f, spectrum.getBottom() - top);
        }
        if (!zones.isEmpty())
        {
            juce::Graphics::ScopedSaveState hatchState(g);
            g.reduceClipRegion(zones);
            g.setColour(juce::Colours::white.withAlpha(0.22f));
            const auto span = spectrum.getHeight();
            for (auto x = spectrum.getX() - span; x < spectrum.getRight(); x += 7.0f)
                g.drawLine(x, spectrum.getBottom(), x + span, spectrum.getY(), 1.0f);
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
}
