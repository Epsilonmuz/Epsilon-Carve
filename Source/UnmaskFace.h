#pragma once

// The Unmask face: the sidechain as a face, the track as an eight-shard mask on a log frequency scale.

#include "MeterHub.h"

namespace carveui
{
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
        juce::ignoreUnused(look);
        source.setTooltip("What is in the sidechain? This only changes the labels.");
        source.onClick = [this] { showSourceMenu(); };
        addAndMakeVisible(source);
        refreshSource();
    }

    ~UnmaskFace() override = default;

    void resized() override { source.setBounds(getWidth() - 16 - 128, 9, 128, 24); }

    void refreshSource()
    {
        const auto index = sourceIndex(params);
        source.setButtonText(sourceNames[static_cast<std::size_t>(index)]);
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
            // Shards never cross the frequency labels on the right of the face.
            juce::Path faceSide;
            faceSide.addRectangle(-400.0f, -400.0f, 574.0f, 1000.0f);
            g.reduceClipRegion(faceSide, art);
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
    // Styled like the preset menu: panel font, quiet frame, a chevron.
    class SourceButton final : public juce::Button
    {
    public:
        SourceButton() : juce::Button("Making room for") {}
        void paintButton(juce::Graphics& g, bool over, bool down) override
        {
            const auto area = getLocalBounds().toFloat().reduced(0.5f);
            g.setColour(down ? palette::edge : palette::plot);
            g.fillRoundedRectangle(area, 6.0f);
            g.setColour(over ? palette::dim : palette::edge);
            g.drawRoundedRectangle(area, 6.0f, 1.0f);
            g.setColour(palette::text);
            g.setFont(font(14.0f, Weight::semibold));
            g.drawText(getButtonText(), area.withTrimmedRight(22.0f), juce::Justification::centred);
            juce::Path chevron;
            const auto cx = area.getRight() - 14.0f, cy = area.getCentreY();
            chevron.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
            g.setColour(palette::muted);
            g.fillPath(chevron);
        }
    };
    SourceButton source;
    juce::Path head, neck, features, lips, lowerLip, mouth, mask, engraving;
    std::array<juce::Path, faceRegions * 2> shards;
    struct Drift { float x = 0.0f, y = 0.0f, z = 0.0f; };
    std::array<Drift, faceRegions * 2> shardDrift;
};
}
