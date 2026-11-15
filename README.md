# Nektar (Νέκταρ)

Prisma Audio — a synthesizer / audio engine that works only from samples.
JUCE, Apple Silicon only. One codebase builds both the Standalone app and the VST3.

## Build
GitHub → Actions → "Build Nektar (macOS, Apple Silicon)" → Run workflow.
The result is `Nektar-macOS-arm64.zip` under the run's Artifacts (contains `Nektar.vst3` and `Nektar.app`).

## Status
Step 9: Resonator tuned to the held keys (Tune, Decay, Material, Mix). Morph between slots (1<-2, 3<-4), Bright, Blur, Freeze. Cleaning per slot, four slots, mic recording.
