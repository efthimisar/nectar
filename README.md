# Nektar (Νέκταρ)

Prisma Audio — a synthesizer / audio engine that works only from samples.
JUCE, Apple Silicon only. One codebase builds both the Standalone app and the VST3.

## Build
GitHub → Actions → "Build Nektar (macOS, Apple Silicon)" → Run workflow.
The result is `Nektar-macOS-arm64.zip` under the run's Artifacts (contains `Nektar.vst3` and `Nektar.app`).

## Status
Step 6: four slots, each with its own sound, Level, Pitch and Tail (click a slot to choose a file or select it; drop a file on a slot). Stretch knob (Time module); pitch without changing length (Keep length); smooth sustain loop (Loop); microphone recording in the standalone. Other knobs are not wired yet.
