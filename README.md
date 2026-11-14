# Nektar (Νέκταρ)

Prisma Audio — a synthesizer / audio engine that works only from samples.
JUCE, Apple Silicon only. One codebase builds both the Standalone app and the VST3.

## Build
GitHub → Actions → "Build Nektar (macOS, Apple Silicon)" → Run workflow.
The result is `Nektar-macOS-arm64.zip` under the run's Artifacts (contains `Nektar.vst3` and `Nektar.app`).

## Status
Step 8B: Morph - slot 1 takes its colour (spectral envelope) from slot 2, slot 3 from slot 4. Bright, Blur and Freeze in the Spectral module. Cleaning per slot, four slots, mic recording.
