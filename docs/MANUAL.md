# Epsilon Carve v0.4.0 — User Manual

Epsilon Carve makes room for one sound inside another. You put it on the sound that needs to step aside (the **track**, usually the beat or an instrument bus) and feed it the sound that needs space (the **sidechain**, usually the vocal). Whenever the sidechain plays, Carve dips the track at the sidechain's frequencies and lets go when it stops. Nothing is delayed: Carve adds zero latency, so it is safe while recording and monitoring live.

## The display

| What you see | What it means |
| --- | --- |
| **Amber area** (TRACK) | Level of the track in each of Carve's 32 bands, before any cut. |
| **Cyan line** (SIDECHAIN) | Level of the sidechain in each band. |
| **Coral columns** (COLLISION) | Frequencies where Carve is cutting right now. Brighter means deeper. |
| **Lower graph, white line** | The exact cut being applied, in dB, worked out from the live filter state (all 32 sections, Mix included). |
| **Dashed cyan line** | Shown when Width Guard is on: the cut applied to the stereo sides (flat at 0 dB means the width is untouched). |
| **White handles** | Low Cut and High Cut. Drag them left and right. The shaded area outside them is never cut. |
| **PEAK CUT** | The deepest cut at this moment and its frequency. |
| **Status badge** | **SIDECHAIN LIVE**: the sidechain is arriving. **SIDECHAIN QUIET**: the sidechain is routed but silent. **NO SIDECHAIN ROUTED / SELF**: the host has not connected a sidechain, so the track drives itself. **SELF MODE**: you chose Self Mode. |
| **Bottom line** | Hover anywhere on the graph to read the frequency, track level, sidechain level and cut at that point. |

## The controls

**Amount** (0–100%, default 25%). The main control: how hard Carve cuts. At 100% a single band can be asked for up to 18 dB of cut. Start at 25–40% and raise it until the sidechain sits clearly, then back off a touch. Double-click any knob to reset it.

**Low Cut** (20 Hz – 20 kHz, default 20 Hz). The lowest frequency Carve may touch. Raise it to keep kick and 808 weight intact while still clearing room above. 100–150 Hz is typical for vocals.

**High Cut** (20 Hz – 20 kHz, default 20 kHz). The highest frequency Carve may touch. Lower it to leave hi-hats, cymbals and air alone. 6–10 kHz is typical for vocals. If Low Cut is set above High Cut, nothing is cut and the display says so.

**Sensitivity** (−48 to +24 dB, default 0 dB). How strongly the sidechain counts. Turn it up for quiet sources (a soft vocal, a distant mic) so they still trigger cuts. Turn it down if Carve reacts to things you don't want it to, such as breaths or bleed. In Collision mode, raising Sensitivity also asks for a little more clearance.

**Attack** (1–150 ms, default 15 ms). How fast the cut arrives when the sidechain comes in. Short (1–5 ms) for kicks and snares, longer (10–20 ms) for vocals so the transients of the beat aren't softened unnecessarily.

**Release** (10–1000 ms, default 180 ms). How fast the track recovers after the sidechain stops. Short release follows a rapper's syllables. Long release gives a smoother, less noticeable movement.

**Mix** (0–100%, default 100%). Blend of the carved signal and the original. Lower values give a gentler, parallel version of the cut.

**Collision / Classic** (default Collision).

- **Collision** listens to both the track and the sidechain. In each band it asks: is the track loud enough here to cover the sidechain? If not, the band is left alone. If yes, it cuts just enough for the sidechain to sit about 9 dB clear, never more than Classic would. Result: the beat stays fuller, especially in quieter sections, and the cuts land exactly where the clash is.
- **Classic** cuts in proportion to how loud the sidechain is in each band, whatever the track is doing. Use it for kick-vs-808 and snare pockets, where you want a predictable duck every hit.

Collision needs a routed sidechain. Without one, Carve uses Classic behaviour automatically.

**Width Guard** (default off). Carve works in Mid/Side. With Width Guard on, only the centre (Mid) of the track is cut; the sides keep their full level, so a wide beat stays wide while the vocal gets room in the middle where it sits. Switching it on and off glides over 30 ms, so it never clicks.

**Delta** (default off). You hear only what Carve is removing. Use it to check that it is cutting the right part of the track, then turn it off. If you bounce with Delta on, you will export only the removed signal.

**Sidechain In / Self Mode** (default Sidechain In). Sidechain In uses the routed sidechain. Self Mode makes the track carve itself: it becomes a 32-band dynamic EQ that tames whatever is loudest in the track, a quick way to calm a harsh or boomy bus.

**Presets.** Use the arrows or the menu at the top. The presets are also exposed to your DAW as programs.

| Preset | For |
| --- | --- |
| Init | Neutral starting point. |
| Vocal Space | Lead vocal over a full beat. Collision, Width Guard on, 120 Hz – 8 kHz. |
| Rap Vocal Pocket | Fast delivery: faster attack and release, a bit more sensitivity. |
| Lead Over Beat | Synth or guitar lead over a beat. |
| Kick Through 808 | Classic mode, very fast attack, 30–250 Hz. Send the kick to the sidechain, put Carve on the 808. |
| Snare Pocket | Classic, 1 ms attack, for snare cracks. |
| Clean Low Mids | Only 200–700 Hz, for mud between two parts. |
| Voice-Over Bed | Heavy, slow carve of a music bed under speech. |
| Air For The Hook | Top-end only, 3–16 kHz, for vocal presence and air. |
| Gentle Glue | Light full-range Collision carve. |

## Under the hood

1. **Analysis.** Both the sidechain and (in Collision mode) the track pass through 32 band detectors spaced evenly on a log scale from 20 Hz to 20 kHz (about three per octave). Each detector is two cascaded band-pass filters, so a loud note in one band barely registers four octaves away (about −63 dB).
2. **Envelopes.** Each band's level is followed with your Attack and Release times.
3. **Decision.** Every 16 samples (a third of a millisecond) Carve works out how much each band should be cut. Classic: proportional to the sidechain level. Collision: the smaller of the Classic cut and the cut needed for 9 dB of clearance, ignoring sidechain bands more than 36 dB below its loudest band (they are inaudible anyway).
4. **Range.** The Low Cut and High Cut handles fade bands in and out smoothly, so a handle can sit between two bands.
5. **Cutting.** The track is split into Mid and Side. Each passes through 32 cascaded dynamic cuts at the same frequencies (state-variable filters with fixed poles, so they cannot become unstable while moving). Each band's bandwidth is held constant in octaves up to the top of the audio range, so cuts are just as deep at 12 kHz as at 1 kHz. Cut depth changes are smoothed over 4 ms to avoid zipper noise.
6. **Output.** Mid and Side are recombined, Mix is applied, and the result goes out with no delay. At Amount 0, or with a silent sidechain in Collision mode, the output is bit-for-bit identical to the input.

## Tips

- Compare with bypass at the same loudness. Carving makes the track slightly quieter, and quieter often sounds worse at first.
- If the beat "pumps", lengthen Release or lower Amount. If the vocal still gets buried on loud words, shorten Attack or raise Sensitivity.
- Use Delta for 10 seconds on every new song to hear exactly what's leaving.
