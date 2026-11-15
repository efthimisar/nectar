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

## Step 12 - Pan and Intermorph
- Each slot is rendered into its own buffer and mixed by SlotMixer. Params: pan, pan2, pan3, pan4 (-1..1, equal power, 1.0 in the middle) and imorph12 / imorph34 (0 = off; above 0 it sets the crossover, 100 Hz to 8 kHz, log).
- Intermorph: low band of slot 1 (3) + high band of slot 2 (4) through a Linkwitz-Riley crossover. In this mode slot 2 (4) is heard only above the crossover and slot 1 (3) only below it. Needs both slots loaded.
- Test (Linux): identical sound in both slots gives exactly the single sound (flat sum), pan gains as designed. Not yet heard. Knobs on the slots (chosen by the author): row 1 = Level, Pitch, Pan (+ Inter on slots 1 and 3), row 2 = Noise, Hum, Low cut, Tail; knob radius 14 and spacing 40 so that four fit in a row. Layout not yet seen on screen.

## Step 13 - Resample
- The circular-arrow button in the top bar (next to room tone) records the output of the synth (all slots, resonator, Tilt) into the selected slot; click again to stop (limit 60 s). Works in the plugin too, not only the standalone. The new sound is named "Resample <date-time>" and goes through the normal analysis and cleaning, then the sine model.
- Test (headless, Linux): processor loaded with nord pad 1, a held note recorded for 4 s: the sample in slot 2 has the same RMS as the live output (0.0247) and 2 channels.
- Not yet: stopping by key, recording only while a note sounds, a tail after the last note.

## Step 14A - Presets
- "Presets" pill in the top bar (left of the Rec button): menu with "Save preset..." and the list of saved presets. Files: `~/Music/Nektar Library/Presets/<name>.nektarpreset` (XML: all knob values + the path of each slot's sound).
- A sound without a file of its own (a recording or a resample) is written as WAV into `~/Music/Nektar Library` when the preset is saved, so that the preset can find it again. A missing file is reported on loading.
- Test (headless, Linux): save, change two knobs, load: values restored; resampled slot restored from the library.
- Not yet: rename/delete, folders, preset browser in the main window, factory presets.
- Next in step 14: modulation (LFO, slicer, filter) and effects: to be specified with the author.

## Step 14B – Modulation and single-panel UI
- Every knob can take a modulation source: right-click the knob (None / LFO 1 / LFO 2 / Slicer), Shift+drag edits the amount (orange ring).
- LFO 1 / LFO 2: rate + shape (sine, triangle, saw, square, sample-and-hold). Routes are saved with the state and presets.
- The editor is now one panel: four slots, library ring, and a 3x3 grid of modules (Filter, Reverb, Delay and Slicer are placeholders until the next steps).

## Step 14C – Slicer
- Rhythmic gate after the resonator: Rate (1, 2, 4, 8 steps per beat), Pattern (8 own-design 16-step patterns), Smooth (0.3–25 ms edges), Depth (0 = off).
- Follows the host tempo and position; free-runs at 120 bpm (or the host tempo) when the host is stopped or absent (standalone).
- The gate is also a modulation source: right-click any knob, choose Slicer.

## Step 14D – Filter with character
- Type knob in four zones: Ladder (JUCE `dsp::LadderFilter`, 24 dB/oct LP; position in the zone = drive), SVF (LP -> BP -> HP morph, TPT form after Zavalishin), Comb (Cutoff = pitch, Reso = feedback), Formant (three band-pass filters, Cutoff sweeps vowels i-e-a-o-u).
- Formant frequencies: average adult male American English F1/F2/F3 from Peterson & Barney (1952), J. Acoust. Soc. Am. 24, 175-184 (values read from the Princeton COS 325 reprint and MIT 6.541J table 6.2). Relative formant levels are our own choice.
- Mix = 0 switches the filter off. Position: after the resonator, before the slicer.

## Step 14E – Melodic reverb
- 8-line feedback delay network with a Hadamard matrix (the FDN idea after Jot & Chaigne 1991 / Stautner & Puckette 1982; named from general knowledge, the papers were not checked). Two allpass diffusers at the input, a damping low-pass in every line.
- Own design: the lines are tuned to root x (1, 3/2, 2, 3, 4, 5, 6, 8), so the tail rings as a chord. Note = root (A1 .. A4, semitone steps; changing it glides).
- Shimmer: octave-up shifted copy of the tail fed back. It is self-limiting and the maximum decay is lowered as shimmer rises (20 s at 0, 6 s at 1). With a very high Note and strong Shimmer the tail can hold as a drone.
- Decay: 0.3 .. 20 s. Mix = 0 switches the reverb off. Position: after the filter, before the slicer.

## Step 14F – Melodic delay
- Ping-pong delay (left, right, left ...) in tempo steps (1/16 note up to 1 bar, 8 steps; host tempo, 120 bpm if none). Time changes glide.
- Pitch: every repeat is shifted again by the same number of semitones (-12 .. +12, centre = off), so repeats climb or fall like a melody. Each repeat is also darker. Feedback up to 0.92, with a soft limiter in the loop.
- The pitch shifter (two crossfaded read taps) is our own simple implementation. Measured: with +12 the repeats of a 440 Hz tone were at 440, 862, 1704, 3386 Hz; with -6 at 440, 312, 222, 158 Hz.
- Mix = 0 switches it off. Order after the reverb and before the slicer.

## UI: wide layout with size hierarchy
- Window is now wide (1100 x 665 design units, default 1200 px wide, aspect locked). Hierarchy by size: Library ring largest, four slots medium, nine round modules smallest in three pyramids of three (Source/Time/Body, Filter/Resonator/Slicer, Reverb/Delay/LFO). Top row: Slot 1, Slot 2, Library, Slot 3, Slot 4.

## Output safety
- A soft knee limiter at the end of the chain (identity below 0.9, never above 1.0) keeps reverb, delay and shimmer peaks from clipping. It sits before Resample, so resampled sounds are limited too.

## Resample safety
- Resample asks once before it replaces a slot that already has a sound (click again within 5 s).
- Auto-stop: after the sound has been heard and 2.5 s of silence follow, the take ends and keeps 0.4 s of tail.

## Modulation sources and Bitcrusher (build 15)
- **ENV**: ADSR driven by the held keys (global, one envelope; a new key restarts the attack). Unipolar 0..1. Times: attack/decay 1 ms–5 s, release 10 ms–8 s.
- **JOY**: round XY pad (Joystick X / Y as sources). See build 17 for the current behaviour.
- **LFO**: the shape knob selects 17 shapes (see build 17); the first 12 are: sine, triangle, saw up, saw down, square, pulse 25 %, sample and hold, smooth random, stairs, half sine, exponential decay, double sine. Old presets that used the shape knob will sound different.
- **Slicer 2 / 3**: extra sources on the Slicer's step clock - a 16-step staircase and a fixed 16-step level sequence (our own tables).
- **CRUSH**: bits 3–16, sample-rate reduction 1–32x, TPDF dither, mix; sits after the Filter, before the Reverb.
- Right-click a knob to choose any of the sources (LFO 1/2, Slicer, Slicer 2/3, Envelope, Joystick X/Y).

## SPACE: placement around the head (build 16)
- Each slot has a direction (-180..180 deg, 0 = front, 90 = right) and a distance, set by dragging the numbered dots on the SPACE pad (top view, front at the top). The knobs: Amount (0 = old plain pan, 1 = fully placed), Speakers (check mode), Far (how much distance lowers level and brightness), Back (how dark it gets behind the listener). Direction and distance of every slot can also be modulated.
- For headphones. Structure after Brown & Duda (1998) *A structural model for binaural sound synthesis*: interaural time difference from Woodworth's spherical-head formula (Duda & Martens 1998 support it as a good approximation) and a one-pole/one-zero head-shadow filter per ear. Head-shadow filter and coefficients (alpha_min 0.1, theta_min 150 deg, pole at c/a) verified against the paper, Eqs. 2-5. Darker sound behind, and the distance law (level and high-frequency loss) are our own design.
- Speakers mode: plain pan from the direction plus distance and back darkening, no time or head-shadow cues (they would only colour the sound on speakers).
- Measured: ITD at 90 deg = 32 samples at 48 kHz (0.67 ms); the slot's stereo width is reduced to mono when placed.
- The editor is now 1360 wide (the SPACE pad and module sit on the right).

## JOY like the Hartmann Neuron, 17 LFO shapes (build 17)
Source: Hartmann Neuron user manual (2002), stick animation p. 28, 89-92 and basic setting StkRec start p. 41.
- **Rec = standby**: the button arms the recording; it starts when the stick moves or a key is pressed (the manual offers Key or Stick as the trigger; we accept either). The pad dot blinks orange while waiting, red while recording. Arming and stopping without a trigger keeps the old path. A new recording replaces the path without a question (as in the manual).
- **Play** (button cycles Off, 1shot, Repeat, Free): 1shot = the first key (with no key held) starts the path once; later keys do nothing while it runs and the value stays where it ends. Repeat = it cycles while keys stay held, stops when all are released and stays there. Free = the old always-looping behaviour (no key needed).
- The path (200 points/s, up to 30 s) and the stick position are saved in presets and in the host state. Loading a preset never starts a recording.
- The manual describes the Neuron stick as four cross-x parameters with opposite pairs; our pad gives X and Y as modulation sources, so opposite pairs come from a positive and a negative amount.
- **LFO shapes**: five shapes added after the Neuron manual p. 118 (positive triangle, positive sine, positive square, stepped triangle, stepped saw down), 17 in total. The positive ones run 0..1. The number of steps in the stepped shapes is read off the picture, not specified. Old presets that use the shape knob change sound again (none exist yet).

## Velocity and Key sources (build 18)
- Two new modulation sources in the right-click menu: **Velocity** (last note-on velocity, 0..1, held after release) and **Key** (last note relative to C3 = MIDI 60, -1..1 over +-3 octaves, clamped). Both are block-rate like the other sources. The +-3 octave span is our choice, not the manual's.
- SPACE position (azimuth, distance) already follows any source through the generic route; the pad still draws the unmodulated dot.

## Crossmorph inside a slot pair (build 19)
- New **Cross** knob on slots 2 and 4 (one per pair 1+2 and 3+4). Centre = both slots at full level; towards 0 only the first slot sounds, towards 1 only the second. Equal-power law, each side capped at 1, so the centre position leaves the sound unchanged.
- It is a normal knob, so the Neuron's two crossmorphs are made by routing: **Velocity** to Cross = velocity crossmorph (soft keys one slot, hard keys the other; set the knob at 0 and amount +1 for a full swing). **Envelope** to Cross follows the note's ADSR (our own use; the manual's dynamic crossmorph is a timed crossfade, see build 22). The Velocity crossmorph matches the manual (p. 98: weakest key = only resynator 1, strongest = only resynator 2).

## Snapshots A and B (build 20)
- The preset menu has "Store snapshot A/B" and "Recall A/B". A snapshot keeps every knob, all modulation routes and the joystick path, so two settings can be compared by ear. The four sounds are not part of a snapshot, and snapshots live in memory only (lost when the plugin closes, not saved with the project): save a preset to keep a setting.
- Our own simple version for comparing settings; the Neuron manual's snapshot behaviour was not copied.

## Second Resonator, "Mix" (build 21)
- Resonator B runs beside the first one on the same input (parallel): out = dry + ring A + ring B. New knobs **Tune B** (default +7 semitones, a fifth) and **Mix B** (0 = off) sit just under the Resonator circle; B shares **Decay** and **Material** with A, so one more pair of knobs is all it costs. Both new knobs take modulation like every other knob.
- Check: B with the same tune as A gives the same level as A; at +7 st it differs; A + B adds up. Measured on a pad sound with a held note.
- **Not built:** the Neuron's "Chromophonic" and "Dual sphered" modes (the earlier note said they need another architecture, with the resonator inside each slot). They need the manual's description to be built faithfully: send the page if you want them.

## Ramp source and the Blender types (build 22)
Source: Hartmann Neuron manual, Blender types p. 96-98.
- New modulation source **Ramp**: after the first key of a phrase (no key held before) it rises from 0 to 1 in the **Ramp** time (knob under the ENV circle, 0.05 to 20 s, exponential knob law) and stays at 1; a new phrase restarts it. Routed to **Cross** it is the manual's dynamic crossmorph (timed crossfade, "up to 20 seconds"); it can also drive any other knob.
- Blender types in the manual: Mix, Mix singlesphere, Stereo, Chromophonic (scape 1 through sphere 2 and scape 2 through sphere 1), Dual sphered (scapes mixed, then through sphere 1 and 2 in turn), Intermorph (have: Inter knob), Dyn. transsphere, Dyn. crossmorph, Velo crossmorph, Velo chrome. **Not built:** Chromophonic, Dual sphered, Dyn. transsphere and Velo chrome need a separate "scape" (source) and "sphere" (body/spectral envelope) per slot that can be exchanged; our Morph blends partials of two sounds, which is a different thing. Open for a decision.

## Build 23: Chromophonic (Chroma 1+2, Chroma 3+4, Move)
Σε κάθε ζεύγος slots (1+2, 3+4) το καθένα παίρνει το «σώμα» του άλλου (χρονικός μέσος όρος της ενέργειας των partials ανά 1/12 οκτάβας, εξομαλυμένος ±1/3 οκτάβας, όριο ±12 dB), με το μοντέλο partials. Το Chroma ρυθμίζει πόσο (0 = κλειστό). Το Move προσθέτει αργό φίλτρο που ταξιδεύει στο φάσμα (κύκλος ~6 δευτ., ±14 dB στο μέγιστο, μετατοπισμένο στο δεξί κανάλι). Η ένταση εξισώνεται κατά ενέργεια σώματος. Δεν έχει ακόμη: knobs απόστασης/φωτεινότητας του πίσω μέρους, low cut 300 Hz, ξεχωριστή ουρά στο μπροστινό. Η πηγή του αλγορίθμου είναι δικό μας πρωτότυπο (ακροάσεις), όχι δημοσιευμένη μέθοδος.

## Build 24: Auto level και LFO Fade in / Fade out
- Auto level: κάθε sample μετριέται με A-weighting στα σημεία που ηχεί και φέρνεται σε κοινή στάθμη (διακόπτης στο μενού Preset, ενεργός). Ο στόχος είναι ρυθμισμένος ώστε ένα τυπικό sample να μένει περίπου όπως ήταν (δική μας επιλογή, όχι δημοσιευμένη μέθοδος). Ο τύπος A-weighting είναι από το IEC 61672.
- LFO Fade in / Fade out (κάτω από τον κύκλο LFO): οι δύο LFO μπαίνουν σταδιακά μετά το πρώτο πλήκτρο μιας φράσης (έως 10 δευτ.) και προαιρετικά σβήνουν.
