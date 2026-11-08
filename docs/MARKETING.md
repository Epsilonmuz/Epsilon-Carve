# Epsilon Carve — positioning notes (internal)

## The one-line pitch

**Carve makes room for the vocal only where the beat is actually in the way.**

Every sidechain EQ on the market ducks wherever the vocal is loud. Carve is the one that also *looks at the beat* before it cuts. That is the story, and the Collision display shows it in one glance: amber beat, cyan vocal, coral where they collide.

## The brand image: unmasking

"Masking" is the real audio term for one sound hiding another. Carve's face makes it literal: the vocal is a face, the beat is a mask, and turning Amount lifts the mask off piece by piece. Use it everywhere:

- **Store hero and thumbnail:** the face half-unmasked (`docs/unmask-face-sequence.png`, `docs/carve-unmask.gif`).
- **Tagline:** *Unmask your vocal.*
- **Video:** 5 seconds of Amount 0 → 60% with the shards lifting, then Delta on so buyers *hear* what was removed.
- **Social:** before/after stills of the face with the Vocal Clarity number.

The face artwork is original line art drawn in code. Keep it that way: no real person's face and no recognisable character.

## Claims we can make today (and the evidence)

| Claim | Evidence | Wording to use |
| --- | --- | --- |
| Cuts only where tracks collide | Engine test: a 200 Hz beat note under a 3 kHz vocal note is cut 0.00 dB in Collision mode | "Leaves the beat alone where it isn't in the way." |
| Keeps more of the beat | v0.5.0 synthetic test: same clarity as Classic with 18% less beat removed while busy, 69% less when the beat drops 15 dB | "Keeps your beat fuller. Measured, not guessed." Say "in our tests" until we have listening tests. |
| Uncovers more of the vocal | At 100%: 24% of the vocal still masked vs 36.5% for Classic | "Uncovers what ducking can't." |
| Keeps stereo width | Width Guard test: side channel changed by 0.000 dB | "Width Guard: clear the centre, keep the width." |
| Even across the spectrum | Cut depth within ~4 dB from 55 Hz to 18 kHz (v0.3.0: ~11 dB) | "Same precision at 12 kHz as at 120 Hz." |
| Safe for live use | Zero latency, ~2–3.5% of one core | "Zero latency. Track through it." |

Do **not** claim "AI", "psychoacoustic model", "better than [brand]" or "sounds like [brand]" until a blind listening test backs it.

## Naming and trademarks

- Keep the name **Epsilon Carve**. The early concept art said "Epsilon TrackSpacer" and "Trackspacer®-style": that is another company's trademark. Never use it in the product name, store copy, screenshots, presets or ads. "Sidechain EQ" and "frequency ducking" are the generic terms.
- The concept art also listed AAX and Windows. Don't advertise formats we haven't built and tested (AAX needs an Avid developer agreement).

## Store page skeleton

1. Hero: the Unmask face + collision display at 2x (`docs/epsilon-carve-v0.5.0.png` is the 1x render).
2. Headline: *Unmask your vocal. Carve only where the beat is in the way.*
3. Three tiles: **Unmask face** · **Collision mode** · **Zero latency**.
4. 20-second before/after video: Delta on for 3 seconds so buyers *hear* the collision being removed.
5. Presets list, formats (VST3 / AU, macOS 12+), system load.

## Before charging money

- Mac build + FL Studio / Logic / Ableton routing check.
- Blind A/B listening test against Classic mode (and competitors, internally only).
- JUCE commercial licence, Apple Developer ID signing and notarisation, an installer.
- Keep this repository private.
