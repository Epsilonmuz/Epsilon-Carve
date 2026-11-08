# Epsilon Carve v0.5.2 — User Manual

> The full illustrated manual (recipes, creator's notes, diagrams, annotated screenshots) is **[Epsilon-Carve-v0.5.2-Manual.pdf](Epsilon-Carve-v0.5.0-Manual.pdf)**. This page is the quick text version.

Epsilon Carve makes room for one sound inside another. You put it on the sound that needs to step aside (the **track**, usually the beat or an instrument bus) and feed it the sound that needs space (the **sidechain**, usually the vocal). Whenever the sidechain plays, Carve dips the track at the sidechain's frequencies and lets go when it stops. Nothing is delayed: Carve adds zero latency, so it is safe while recording and monitoring live.

## Simple and Advanced views

Carve opens in **Simple** view: Low Cut, Amount and High Cut, plus the Collision/Classic switch and presets. That is all most sessions need. Click **Show Advanced** to add Sensitivity, Attack, Release, Mix, Width Guard and Delta. Carve remembers the view with your session.

## The Unmask face

The panel on the left is the quickest way to read Carve.

- **The face is your sidechain**: the sound that needs room. Use the picker at the top right of the panel (**Vocal, Kick, Snare, 808 / Bass, Lead, Voice-over, Other**) to name it. The picker only changes labels and suggests a matching preset.
- **The mask is the track** covering it, in the track's amber.
- **The face is a frequency scale.** Top to bottom runs from 20 kHz down to 20 Hz on a log scale, in eight equal slices of 1.25 octaves: 8.4–20 kHz, 3.6–8.4 kHz, 1.5–3.6 kHz, 632 Hz–1.5 kHz, 267–632 Hz, 112–267 Hz, 47–112 Hz, 20–47 Hz. The slice edges are printed beside the face.
- **Each shard shows only its own slice.** It lifts by exactly the share of that slice's masking that Carve has removed: half the masking gone, half the lift. A shard stays in place when its slice is outside your Low Cut / High Cut, or when the track is still covering the sidechain there.
- **A faint, still shard** means the sidechain has almost nothing in that slice, so there is nothing to unmask there.
- **Clarity %** is how much of the sidechain is clear of the track right now, weighted by where the sidechain is loudest. **Peak cut** shows the deepest cut and its frequency.

How it is calculated: in each of Carve's 32 bands, Carve compares the track's level, after the cut, with the sidechain's level. The sidechain counts as clear when the track is 9 dB or more below it (the clearance Collision aims for) and buried when the track is 3 dB or more above it. Bands are weighted by the sidechain's level (anything more than 36 dB below its loudest band is ignored) and shared between neighbouring slices by how much of each band's frequency span falls in each slice. Shards lift quickly and settle back slowly, and the face holds its last state while the sidechain is silent.

## A/B and edited presets

- **A / B** (next to the Sidechain button): two complete settings. Click **B** to try something without losing **A**; click **A** to go back. The arrow copies the active slot to the other. Both are saved with the session.
- An **asterisk** after the preset name ("Vocal Space *") means you've changed something since loading it. Pick the preset again to return to it.

## The display

| What you see | What it means |
| --- | --- |
| **Amber area** (TRACK) | Level of the track in each of Carve's 32 bands, before any cut. |
| **Cyan line** (SIDECHAIN) | Level of the sidechain in each band. |
| **Coral columns** (COLLISION) | Frequencies where Carve is cutting right now. Brighter means deeper. |
| **Spectrum detail** | Both spectra come from a 4096-point analyser with 1/6-octave smoothing, so you see individual notes and harmonics. |
| **Lower graph, white line** | The exact cut being applied, in dB, worked out from the live filter state (all 32 sections, Mix included). |
| **Dashed cyan line** | Shown when Width Guard is on: the cut applied to the stereo sides (flat at 0 dB means the width is untouched). |
| **White handles** | Low Cut and High Cut. Drag them left and right. The shaded area outside them is never cut. |
| **PEAK CUT** | The deepest cut at this moment and its frequency. |
| **Status badge** | **SIDECHAIN LIVE**: the sidechain is arriving. **SIDECHAIN QUIET**: the sidechain is routed but silent. **NO SIDECHAIN ROUTED**: the host has not connected a sidechain, so the track drives itself. **INTERNAL SIDECHAIN**: you chose Internal. |
| **Bottom line** | Hover anywhere on the graph to read the frequency, track level, sidechain level and cut at that point. |

## The controls

**Amount** (0–100%, default 25%). The main control: how hard Carve cuts. At 100% a single band can be asked for up to 18 dB of cut. Start at 25–40% and raise it until the sidechain sits clearly, then back off a touch. Double-click any knob to reset it.

**Low Cut** (20 Hz – 20 kHz, default 20 Hz). The lowest frequency Carve may touch. Raise it to keep kick and 808 weight intact while still clearing room above. 100–150 Hz is typical for vocals.

**High Cut** (20 Hz – 20 kHz, default 20 kHz). The highest frequency Carve may touch. Lower it to leave hi-hats, cymbals and air alone. 6–10 kHz is typical for vocals. If Low Cut is set above High Cut, nothing is cut and the display says so.

**Sensitivity** (−48 to +24 dB, default 0 dB). How strongly the sidechain counts. Turn it up for quiet sources (a soft vocal, a distant mic) so they still trigger cuts. Turn it down if Carve reacts to things you don't want it to, such as breaths or bleed. In Collision mode, raising Sensitivity also asks for a little more clearance.

**Attack** (1–150 ms, default 15 ms). How fast the cut arrives when the sidechain comes in. Low bands never react faster than two cycles of their own frequency (50 ms at 40 Hz), so bass and 808 cuts stay steady. Short (1–5 ms) for kicks and snares, longer (10–20 ms) for vocals so the transients of the beat aren't softened unnecessarily.

**Release** (10–1000 ms, default 180 ms). How fast the track recovers after the sidechain stops. Short release follows a rapper's syllables. Long release gives a smoother, less noticeable movement.

**Mix** (0–100%, default 100%). Blend of the carved signal and the original. Lower values give a gentler, parallel version of the cut.

**Collision / Classic** (default Collision).

- **Collision** listens to both the track and the sidechain. In each band it asks: is the track loud enough here to cover the sidechain? If not, the band is left alone. If yes, it cuts just enough for the sidechain to sit about 9 dB clear. Result: the beat stays fuller, especially in quieter sections, the cuts land exactly where the clash is, and at high Amount it can uncover quieter, buried parts of the vocal that Classic leaves covered.
- **Classic** cuts in proportion to how loud the sidechain is in each band, whatever the track is doing. Use it for kick-vs-808 and snare pockets, where you want a predictable duck every hit.

Collision needs a routed sidechain. Without one, Carve uses Classic behaviour automatically.

**Width Guard** (default off). Carve works in Mid/Side. With Width Guard on, only the centre (Mid) of the track is cut; the sides keep their full level, so a wide beat stays wide while the vocal gets room in the middle where it sits. Switching it on and off glides over 30 ms, so it never clicks.

**Delta** (default off). You hear only what Carve is removing. Use it to check that it is cutting the right part of the track, then turn it off. While Delta is on, the display border pulses coral with a banner, the face reads DELTA ON, and the Delta button stays visible even in Simple view, because bouncing with Delta on exports only the removed signal.

**Sidechain: External / Internal** (default External). External uses the routed sidechain. Internal makes the track carve itself: it becomes a 32-band dynamic EQ that tames whatever is loudest in the track, a quick way to calm a harsh or boomy bus.

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
3. **Decision.** Every 16 samples (a third of a millisecond) Carve works out how much each band should be cut. Classic: proportional to the sidechain level. Collision: the cut needed for 9 dB of clearance, capped by a ceiling that follows the sidechain's level three times more readily than Classic, and ignoring sidechain bands more than 36 dB below its loudest band (they are inaudible anyway).
4. **Range.** The Low Cut and High Cut handles fade bands in and out smoothly, so a handle can sit between two bands.
5. **Cutting.** The track is split into Mid and Side. Each passes through 32 cascaded dynamic cuts at the same frequencies (state-variable filters with fixed poles, so they cannot become unstable while moving). Each band's bandwidth is held constant in octaves up to the top of the audio range, so cuts are just as deep at 12 kHz as at 1 kHz. Cut depth changes are smoothed over 4 ms to avoid zipper noise.
6. **Output.** Mid and Side are recombined, Mix is applied, and the result goes out with no delay. At Amount 0, or with a silent sidechain in Collision mode, the output is bit-for-bit identical to the input.

## Tips

- Compare with bypass at the same loudness. Carving makes the track slightly quieter, and quieter often sounds worse at first.
- If the beat "pumps", lengthen Release or lower Amount. If the vocal still gets buried on loud words, shorten Attack or raise Sensitivity.
- Use Delta for 10 seconds on every new song to hear exactly what's leaving.
