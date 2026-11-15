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
