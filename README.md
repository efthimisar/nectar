# Nektar (Νέκταρ)

Prisma Audio — a synthesizer / audio engine that works only from samples.
JUCE, Apple Silicon only. One codebase builds both the Standalone app and the VST3.

## Build
GitHub → Actions → "Build Nektar (macOS, Apple Silicon)" → Run workflow.
The result is `Nektar-macOS-arm64.zip` under the run's Artifacts (contains `Nektar.vst3` and `Nektar.app`).

## Status
Step 8A: Spectral module with Bright (tilt), Blur and Freeze. Cleaning per slot (Noise, Hum, Low cut; analysis report; Room tone). Four slots, Stretch, Keep length, Loop, microphone recording. Other knobs are not wired yet.
