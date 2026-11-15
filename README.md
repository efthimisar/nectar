# Nektar (Νέκταρ)

Prisma Audio — a synthesizer / audio engine that works only from samples.
JUCE, Apple Silicon only. One codebase builds both the Standalone app and the VST3.

## Build
GitHub → Actions → "Build Nektar (macOS, Apple Silicon)" → Run workflow.
The result is `Nektar-macOS-arm64.zip` under the run's Artifacts (contains `Nektar.vst3` and `Nektar.app`).

## Status
Step 10A: Body (Tone, Focus, Size) reshapes the spectral envelope while the pitch stays. Resonator tuned to the held keys, Morph between slots, Bright, Blur, Freeze. Cleaning per slot, four slots, mic recording.

## Roadmap
Done: 1 skeleton and design · 2 WAV from keys · 3 mic recording · 4 pitch with constant length + sustain loop · 5 extreme stretch · 6 four slots · 7 cleaning (noise, hum, low cut, room tone) · 8A Bright / Blur / Freeze · 8B Morph (colour of slot 2 into slot 1, slot 4 into slot 3) · 9 Resonator tuned to the held keys · 10A Body (Tone, Focus, Size).

Next, in this order (each step is tested by ear before the next):
- 10B **Source** knobs (Harmonic/Inharmonic, Smooth/Rough, Tight/Spread) with their own module.
- 11 **Full sinusoidal model** (partials + residual, analysis at load time in the background, additive resynthesis, partial matching and interpolation between two sounds). Based on Caetano (2021), Serra & Smith (1990), Rodet & Caetano (2012). Chosen by the author for pads; partial tracking is unreliable for dense or detuned pads, so it is tested on real pad material first.
- 12 **Intermorph** (low band of one slot + high band of another) and **Stereo** (pan per slot), from the Neuron Blender types.
- 13 Resample into a slot.
- 14 Presets, modulation (LFO, slicer, filter) and effects.

Parked: pipes between modules, fullscreen standalone, compact VST3 layout, persistence of unsaved recordings and room tone, soft limiter against clipping.

- UI update (approved mockup): modules Time | Body | Source | Resonator, Tilt (ex-Bright) in the top bar, Freeze in Time, Morph in Body, Blur shown as Spread in Source (Harmonic/Rough are placeholders for step 10B).
- Step 10B (partly): Source Grit (ex-Rough; not psychoacoustic roughness, which needs partials) = random level patches (~60 Hz) per frame, energy kept; Harmonic stays dim until the sinusoidal model (step 11).
- Morph envelope: cross-fade replaced by log-area-ratio interpolation of an all-pole fit on a mel-like axis (O Reilly Regueiro 2010): resonances now sweep instead of just fading; tested on synthetic noise with 2+1 resonances.

## Step 11 - sine-wave model (Model knob)
- On load, each sound is analysed in the background (STFT 4096, hop 512, parabolic peak interpolation, McAuley-Quatieri-style frequency tracking, up to 8192 tracks). Analysis takes about 3-8 s for 15-45 s sounds.
- Source > Model: 0 = normal spectral engine, 1 = additive resynthesis of the tracks, in between = crossfade. Applies to notes started after the knob moves.
- Not yet in the additive path: noise residual (sounds softer, no breath/noise), Body, Blur, Morph. Pitch, stretch and freeze work.
- Linux test: level within 1 dB of original on pads, about 1.4-3.5% of realtime per voice (x86, unoptimised).
- Open: residual model, partial matching for Morph.

## Step 11B - residual (Noise knob)
- Analysis also keeps, per frame, the strength of the sound outside the partial peaks (48 log-spaced bands, bins within +-5 of a tracked peak excluded). Idea: stochastic part of Serra & Smith 1990 (details of their method not re-checked here).
- Source > Noise (0 = none, 1 = original strength) plays it as random-phase shaped noise (1024 FFT, hop 256, overlap-add) following the same position as the sines (stretch, freeze) and scaled with pitch. Noise is crossfaded with Model like the sines.
- Linux release build: about 0.6-2.2% of realtime per voice with noise; analysis 0.35-0.65 s per sound. Level of sines + noise within 1 dB of the original on the test pads.
- Not verified by listening. Limitation: bins of tracks that were dropped (weak, short, over the 96 per frame / 8192 cap) count as noise.

## Step 11C - Morph between two sounds on the sine model
- With Model > 0 and a partner sound set, the Morph knob now morphs the partials (MorphVoice), at the pitch of the slot's sound. Notes started with Morph at 0 do not pick it up until the next note.
- Pairing by harmonic number, following the description in Caetano 2021 (frequency interpolated in cents, amplitude linear), simplified. The full Tellman, Haken & Holloway 1995 paper was NOT available; their feature alignment (manual in Lemur) is not implemented. Sounds are read at the same time position, without time alignment.
- Own design (not published): one f0 per sound (harmonic sum with presence check, median over frames), track labelling within 3% / max 0.3 of a harmonic spacing, "pairable" if both sounds have harmonicity >= 0.5. Otherwise the two sounds are cross-faded (not morphed). Chords and noise stay cross-faded.
- Tests: synthetic harmonic tones (220 Hz / 1/h against 330 Hz odd harmonics) give exactly the expected partial amplitudes at m = 0, 0.5, 1. On real pads only the analysis numbers and CPU (about 2-3% per voice, Linux release) were checked, not the sound.
- Known limits: f0 can be wrong for dense or noisy sounds (a door recording got f0 = 44 Hz with harmonicity 0.77); no listening test yet.
