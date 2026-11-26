# oPlaits

**English** | [日本語](README.ja.md)

(Module ID: `oplaits`)

A Sound Generator for Schwung / Ableton Move. It is an **unofficial port** of
Mutable Instruments' **Plaits** Eurorack module (a macro oscillator): the DSP
source code of the official firmware, published by Emilie Gillet, is bundled
and run as-is. The sound-producing code has not been rewritten or simplified.

- **All 24 engines of the official Plaits** (including the 8 added in firmware
  1.2), selectable with the same numbers and in the same order as on the
  hardware.
- The instrument itself (engine selection, the 24 engines, the internal decay
  envelope, the low-pass gate and the output limiter) is the official
  `plaits::Voice`, called unchanged. The port replaces **only the hardware**:
  pots, CV inputs, trigger input and DAC.
- The DSP runs at the hardware's own **47872.34 Hz in 12-sample blocks** and is
  resampled to 44.1 kHz (see "About the sample rate").
- Tests confirm that the audio this module plays (OUT / AUX) is **identical
  sample for sample** to the official `plaits::Voice` driven directly with the
  same wiring as the hardware, for all 24 engines (see "Verification").

What was **kept, left out and added** compared with the hardware Plaits:

| | |
|---|---|
| Kept | The sound of all 24 engines, HARMONICS / TIMBRE / MORPH, the three attenuverters (FM / TIMBRE / MORPH) and modulation from the internal envelope, the LPG (DECAY / LPG COLOUR), the two outputs OUT / AUX, and firmware details such as the ~1 ms trigger delay |
| Left out | CV inputs other than V/OCT (FM, HARMONICS, TIMBRE, MORPH, MODEL), audio transfer of user data (custom wavetables or DX7 banks), calibration, LED and button handling |
| Added | Playing over MIDI (note = V/OCT + TRIG, velocity = LEVEL), an OUT/AUX mix, volume, automatic 1/16 triggers synced to Move's transport, 16 trigger-step pads, knob motion recording, melody generation (RANDOM), preset pads, and narrowing down the engines in use |

**It has a built-in sequencer.** Press Play on Move and it starts sounding on
every 1/16 in time with the tempo, with no clip or MIDI notes needed (an 8- or
16-step loop). It also plays normally from MIDI notes on a Move track.

---

## Engines

The number on the ENGINE knob (knob 7) is the official firmware's
`patch.engine`. The screen shows a short name.

| No. | Screen | What it is | Hardware bank |
|---|---|---|---|
| 0 | VAVCF | Virtual analog with a filter | added in 1.2 |
| 1 | PHDIS | Phase distortion | added in 1.2 |
| 2 | 6OP1 | 6-operator FM, DX7 patch bank 1 (HARMONICS picks one of 32 patches) | added in 1.2 |
| 3 | 6OP2 | 6-operator FM, bank 2 | added in 1.2 |
| 4 | 6OP3 | 6-operator FM, bank 3 | added in 1.2 |
| 5 | WTERR | Wave terrain | added in 1.2 |
| 6 | STRMC | String machine | added in 1.2 |
| 7 | CHIP | Chiptune (chords and arpeggiator) | added in 1.2 |
| 8 | VA | Virtual analog, two variable waveforms (factory default) | green |
| 9 | WSHAP | Waveshaping | green |
| 10 | FM | 2-operator FM | green |
| 11 | GRAIN | Granular formant | green |
| 12 | ADDIT | Additive harmonics | green |
| 13 | WTBL | Wavetable | green |
| 14 | CHORD | Chords | green |
| 15 | SPEAK | Voice (vowels/formants, SAM, LPC words) | green |
| 16 | SWARM | Swarm of 8 sawtooth waves | red |
| 17 | NOISE | Filtered noise | red |
| 18 | PARTI | Particle noise | red |
| 19 | STRNG | Inharmonic string | red |
| 20 | MODAL | Modal resonator | red |
| 21 | KICK | Analog bass drum | red |
| 22 | SNARE | Analog snare | red |
| 23 | HIHAT | Hi-hat | red |

HARMONICS / TIMBRE / MORPH mean the same thing on each engine as on the
hardware (see the official Plaits manual).

**Per-engine behaviour that matches the hardware (not bugs)**

- **CHIP (7)**: when triggered, this engine does not use the LPG and takes the
  shape of its own envelope from **TIMB> (the TIMBRE attenuverter)**. With
  TIMB> at centre (0) it does not decay, so it keeps sounding after the note is
  released. Turn TIMB> and it decays.
- **6OP1–3 (2–4)**: note length is set by the DX7 patch's own envelopes; the
  LPG is not used. Some patches ring on long after release.
- **KICK, SNARE, HIHAT, STRNG, MODAL**: they have their own envelopes and
  bypass the LPG. Length is set by the engine's own parameters (MORPH etc.).
- **VA (8)**: HARMONICS detunes the second oscillator: centre (50 %) is
  unison, 0 % is two octaves down.

## Building

The repository builds on its own. The official Plaits source is bundled in
`src/plaits/mi/` and Schwung's plugin API header (`plugin_api_v1.h`, MIT) in
`third_party/schwung/`.

```bash
scripts/build.sh arm64
```

→ `dist/oplaits-module.tar.gz` (AArch64 `dsp.so` for Move + UI + licences)

Requirements: `aarch64-linux-gnu-g++`, `python3`. During the build
`scripts/check_abi.py` checks that the library is AArch64, needs glibc 2.34 or
older, does not depend on libstdc++, and exports only `move_plugin_init_v2`;
the build fails otherwise.

The test suite (comparison with the official source, native ASan/UBSan engine
tests, a dlopen smoke test of the real `dsp.so`, UI tests, and finally the
arm64 build), and the check of what the patches change:

```bash
scripts/test.sh
scripts/compare_upstream.sh
```

The UI tests need Node.js and a Schwung checkout (for its page engine and
render harness): `../schwung` next to this repository, or
`SCHWUNG_ROOT=/path/to/schwung`. Without one, only the UI tests are skipped
(on WSL the Windows `node.exe` also works).

CPU measurement on the Move (needs SSH; the benchmark is placed temporarily in
`/data/UserData/schwung/` on the Move, run, and removed afterwards):

```bash
scripts/bench_move.sh ableton@move.local
```

## Installing on Move

Upload through Schwung Manager (`http://<Move's IP>:7700`):

```bash
scripts/install.py http://move.local:7700                                   # the one in dist/
scripts/install.py http://move.local:7700 --package oplaits-module.tar.gz   # a downloaded one
```

(`--check` only tests the connection.) After installing, choose **oPlaits** as
the Synth of a chain slot.

**Restart Move after an update.** The old `dsp.so` stays loaded in the process,
so re-selecting the slot does not pick up the new version.

## Getting started

The basic flow is: put it in a slot → play → shape the sound with the knobs →
build a pattern on the pads → record knob movements with REC.

### 1. Make a sound

1. Open a slot in the Schwung screen (Shift+Vol+Track, or long-press a track
   button) and choose **oPlaits** as the Synth.
2. Open the slot's Synth (oPlaits) to get the oPlaits screen.
3. Press **Play** on Move. oPlaits **sounds automatically on every 1/16 in time
   with the transport** (no clip or MIDI notes needed). It is silent while
   stopped. It also plays from MIDI notes on a Move track.

### 2. Shape the sound (main page)

| Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|
| VOLUME | HARMONICS | TIMBRE | DECAY |
| **Knob 5** | **Knob 6** | **Knob 7** | **Knob 8** |
| COLOR | MORPH | ENGINE (24) | PITCH |

- **ENGINE** picks the kind of sound; **HARMONICS / TIMBRE / MORPH** shape it
  (the hardware's three big pots).
- **DECAY** and **COLOR** are the hardware's shift functions (LPG DECAY and LPG
  COLOUR). DECAY sets the length, COLOR the character of the LPG (0 % = more
  VCA, 100 % = more low-pass filter).
- **PITCH** is the pitch of the automatic triggers, chosen from the notes of a
  scale (set on the Scale page).
- **Narrow down to the engines you use**: the ENGINES pad (left of RANDOM)
  opens a list where you switch on only the engines you want; the ENGINE knob
  then chooses only among those (see "Choosing engines (ENGINES list)").
- **Turn the jog wheel** to move to the Plaits page (attenuverters, AUX, pitch
  and input settings) → Scale (ROOT, SCALE, STEPS) → My Presets (presets) →
  Module.

### 3. Build a pattern (pads)

While the screen is open the pads belong to oPlaits (they do not reach Move).

```
                         ENGINES RANDOM  REC   ← right three of the top row
 P1  P2  P3  P4  P5  P6  P7  P8                ← second row from the top (presets)
  9  10  11  12  13  14  15  16                ← second row from the bottom
  1   2   3   4   5   6   7   8                ← bottom row
```

- Press a pad in the **bottom two rows** to switch that step's trigger ON
  (bright) / OFF (dim). The white one is the step playing now. With 8 steps the
  second row from the bottom is dark (16 steps: STEPS on the Scale page).
- **RANDOM** writes a new melody into PITCH (see "RANDOM").
- **ENGINES** opens the ON/OFF list of the engines the ENGINE knob can choose.
- The 8 pads of the **second row from the top** are presets (see "5. Saving and
  recalling presets").

### 4. Record knob movements (REC)

1. Press **REC** (the top-right pad) to switch it ON (solid red; OFF is pale red).
2. With Move playing, **touch and turn** the knob you want to record. While you
   touch it, its value is recorded into each step of the loop. Let go to stop.
3. A recorded knob replays its movement on every pass of the loop from then on.
   The knob's own value is not overwritten; clearing the recording returns to it.
4. To clear: **hold Shift and touch a knob** to clear that knob's recording.
   **Shift + touch knob 1 (VOLUME)** clears everything.
5. When you are done, press REC again to switch it OFF so that a stray touch
   records nothing.

Recordable: HARMONICS, TIMBRE, MORPH, DECAY, COLOR, PITCH, FM>, TIMB>, MORPH>,
AUX (VOLUME, ENGINE, COARSE, FINE, VELO, TRIG and the Scale page are not recorded).

### 5. Saving and recalling presets

**On the pads (eight, quickly):** the 8 pads of the second row from the top are
presets 1–8.

- **Save**: hold Shift and tap a pad. It stores the whole current state.
- **Recall**: tap a pad. It switches at once, even while playing (the engine
  switches too).
- Colours: dark = empty, pale yellow = saved, white = the last one recalled (or
  saved). Tapping an empty pad only shows `EMPTY` on screen and changes nothing.
- Stored: every knob value including ENGINE, recorded knob movements, the
  melody made by RANDOM, the step ON/OFF pattern, ROOT / SCALE / STEPS (not the
  REC state or the ENGINES list).
- The eight presets are independent of the set and are saved to
  `/data/UserData/schwung/oplaits/presets.json`, so the same eight are available
  from oPlaits in any set and any slot. The file is separate from other
  modules' presets, so they never overwrite each other. Reinstalling keeps it.

**With a name (no limit on how many):** turn the jog wheel to the **My Presets**
page near the end.

- **Save As**: saves the current state under a name (a keyboard appears).
- **Preset** (top row): click to open the preset list and load the one you pick.
- **Save**: overwrites the loaded preset. **Delete**: deletes the loaded preset.

This is Schwung's common preset feature, saved in a folder per module ID
(`/data/UserData/schwung/presets/oplaits/`).

### 6. When you are done

Close the screen with **Back**. The pads return to Move. The sound, recordings
and step pattern are kept in the set (the slot autosave) and in presets.

## Controls (detail)

The screen and knobs look and work like Schwung's standard knob screen (eight
knobs per page), with knob touch (for recording) and the pads added.

| Control | Action |
|---|---|
| Knobs 1–8 | Parameters of the current page (page table below) |
| Touch a knob (REC ON, playing) | **Motion-record** that knob |
| Shift + touch a knob | Clear that knob's recording; on VOLUME (knob 1), clear **all** |
| Jog turn / click | Change page, select an item (same as Schwung's standard grid) |
| Back | Close the screen (pads return to Move) |

Pages:

| Page | Knobs 1–8 |
|---|---|
| Main | VOLUME, HARMONICS, TIMBRE, DECAY, COLOR, MORPH, ENGINE, PITCH |
| Plaits | FM>, TIMB>, MORPH>, AUX, COARSE, FINE, VELO, TRIG |
| Scale | ROOT, SCALE, STEPS |
| My Presets | Load, save, delete presets (Schwung common) |
| Module | Swap the module, help (Schwung common) |

The header shows the current engine, the loop position, `ARM` while REC is ON
and `REC` while recording. While PITCH is touched it shows the note name (e.g.
`PITCH Eb3`). A knob with a recording shows Schwung's "modulated" display (the
playing value moves).

Stepped knobs (ENGINE, PITCH, COARSE etc.) do not accelerate; they advance one
step every few detents. Continuous knobs (HARMONICS etc.) accelerate when turned
fast (like Schwung's standard knobs).

## Pads

**While this screen is open, oPlaits owns the pads** (`host_pad_block`). Pad
presses do not reach Move's own instruments or sequencer. Close the screen and
the pads return to Move with their colours (Schwung only lets a module own them
while its screen is shown).

| Pad | Action |
|---|---|
| **REC** (top right) | Motion recording ON/OFF. **ON = solid red, OFF = pale red** |
| **RANDOM** (left of it) | Make a new melody and write it into each step of PITCH |
| ENGINES (further left) | Open / close the ENGINES list (engines in use) |
| Second row from the top (8) | Presets 1–8. Tap to recall, Shift + tap to save |
| Bottom two rows (16) | Trigger ON/OFF per step. ON = bright, OFF = dim, playing step = white. With 8 steps the upper row (9–16) is dark |

- Playback **triggers automatically on every 1/16, synced to Move's transport**
  (starts with Play, silent while stopped; steps line up with the bar). At the
  start of each step a trigger goes into Plaits' TRIG. On a step whose trigger
  is OFF the gate goes low and the sound decays naturally through the LPG (or
  the engine's own envelope).
- The loop length is **STEPS** on the Scale page (8 / 16, default 8).
- MIDI notes from the Move track (clips, or the pads while the screen is closed)
  also play oPlaits.

### Motion recording

- With REC ON and Move playing, a knob's movement is recorded into the loop
  **while you touch it** (8 points per 1/16, 12-bit). Let go to stop.
- Continuous knobs are interpolated smoothly between points; PITCH holds each
  point's value.
- Playback does not overwrite the knob value (the recording just takes
  priority). Clearing with Shift + touch returns to the knob value.
- While stopped nothing is recorded or played back, and the knob values sound.

### PITCH (knob 8)

- **PITCH is a scale note from the start.** Knob 8 chooses only the notes of the
  Scale page's ROOT + SCALE, over C1–C6 (five octaves). It is not quantised
  afterwards; the knob itself moves through the scale. Default: C3 in C Phrygian.
- Changing ROOT / SCALE moves PITCH and the recorded PITCH to the nearest note
  of the new scale.
- SCALE: Phrygian (default), Minor, Dorian, Major, Mixolyd, Lydian, Locrian,
  HarmMin, PhrygDom, MinPent, In, Chromatic.
- PITCH is the pitch of the automatic triggers. When played from MIDI notes, the
  note's own pitch sounds.

### RANDOM (melody generation)

RANDOM **makes a slightly different melody each time from the MIDI files (source
phrases) in `patterns/` and writes it into PITCH**. Six source phrases are
included.

**What happens when you press it**

1. One source phrase is chosen at random (the previous one is less likely).
2. Its note sequence is varied with a "50 % amount of change": roughly half of
   the notes change — moving to a neighbouring scale note, jumping an octave,
   repeating the previous note and so on — and the rest stay as in the source.
   The result differs every time and never equals the previous melody.
3. No rests are made. Rests in the source are filled with notes, so every step
   has a pitch.
4. The melody is written into each step of the loop as the PITCH recording.
   - 8 steps with STEPS at 8; 16 steps at 16 (two 8-step variations in a row).
   - The previous PITCH recording is overwritten for the length of STEPS.
   - Whether a step sounds is decided by the bottom two rows of pads. RANDOM
     does not change the step ON/OFF pattern.

**Range and scale**

- Every note written is a note of the scale set on the Scale page (ROOT, SCALE).
- The range is **from a fifth below to an octave and a half above the note of
  knob 8 (PITCH)** (-7 to +17 semitones, inside C1–C6). Notes outside it are
  folded back by octaves. Set the overall height with knob 8 before pressing.

**Undo / redo**

- Press again to rewrite with a different melody.
- **Shift + touch knob 8** clears the PITCH recording, back to knob 8's note only.
- With REC ON, touch and turn knob 8 to rewrite only some steps by hand.

#### Replacing the source phrases

The source MIDI files are embedded into `dsp.so` at build time (the Move does
not read files for this). After replacing them you must **rebuild and
reinstall**.

1. Put the MIDI files you want (Standard MIDI File, `.mid`) in `patterns/`.
2. List the files to use in `patterns/patterns.txt`, one per line (1–16). The
   line order is the phrase number. Anything after `#` is a comment.

   ```
   my_riff.mid          # 8 steps from the start of the file
   long_phrase.mid 16   # 8 steps from step 17 (the second column is the start)
   ```

   Without `patterns.txt`, every `.mid` in `patterns/` is used in name order.
3. Build, install and restart Move.

Which part of a MIDI file is used:

- Only **eight 1/16 notes from the start position** of each file.
- Only **pitch and order** are used. Velocity and note length are not.
- Note positions snap to the nearest 1/16. When a step holds several notes, only
  the strongest (the first, on a tie) is used. All tracks and channels are read
  together.
- Source phrases are first fitted to **C Phrygian** when loaded, varied, and
  finally fitted to the current ROOT and SCALE. Phrases written in C Phrygian
  keep their shape best.
- A file with no note within the 8 steps from its start position is not used.
  File names up to 31 bytes, up to 256 notes per file.

## Choosing engines (ENGINES list)

You can decide which of the 24 engines the ENGINE knob can choose.

1. Press the **ENGINES pad** (left of RANDOM) → the list opens (the `<` on the
   right marks the engine sounding now).
2. Turn the jog wheel to move the cursor (it stops at the ends); click the jog
   to switch **ON / OFF** (■ = ON, □ = OFF).
3. **ALL ON** at the top switches all on; **ALL OFF** switches off all but the
   engine sounding now.
4. Close the list with **Back** or the ENGINES pad.

- Changes are saved at once (`/data/UserData/schwung/oplaits/settings.json`).
  Shared by oPlaits in every slot, kept across reinstalls, not part of presets.
- The ENGINE knob steps through only the engines that are ON and stops at both ends.
- At least one stays ON ("ONE MUST STAY ON").
- A preset that uses an engine switched OFF still plays with that engine (the
  list only limits what the knob can choose).

## Parameters

| Page | Knob | Name | Range (default) | Hardware equivalent |
|---|---|---|---|---|
| Main | 1 | VOLUME | 0–100 % (50 %) | Output level of this module (not on the hardware) |
| | 2 | HARMONICS | 0–100 % (50 %) | HARMONICS pot |
| | 3 | TIMBRE | 0–100 % (50 %) | TIMBRE pot |
| | 4 | DECAY | 0–100 % (50 %) | LPG DECAY (MORPH's shift function). Default is the factory value |
| | 5 | COLOR | 0–100 % (0 %) | LPG COLOUR (TIMBRE's shift function). Default is the factory value |
| | 6 | MORPH | 0–100 % (50 %) | MORPH pot |
| | 7 | ENGINE | 0–23 (8 = VA) | Engine selection buttons. Number is `patch.engine`, default is the factory value |
| | 8 | PITCH | scale notes (C3) | Pitch of the automatic triggers (not on the hardware) |
| Plaits | 1 | FM> | -100–+100 % (0) | FM attenuverter. As with no CV in FM, it sets how much the internal decay envelope moves the pitch |
| | 2 | TIMB> | -100–+100 % (0) | TIMBRE attenuverter (internal envelope → TIMBRE). On CHIP, the envelope's shape |
| | 3 | MORPH> | -100–+100 % (0) | MORPH attenuverter (internal envelope → MORPH) |
| | 4 | AUX | 0–100 % (0 %) | Mix of OUT and AUX (below). Not on the hardware |
| | 5 | COARSE | -24–+24 semitones (0) | Stands in for the FREQUENCY pot (in semitones) |
| | 6 | FINE | -100–+100 cents (0) | Stands in for fine tuning of the FREQUENCY pot |
| | 7 | VELO | OFF / ON (OFF) | Whether velocity goes into the LEVEL input (below) |
| | 8 | TRIG | ON / OFF (ON) | Whether the TRIG input is "patched" (below) |
| Scale | 1 | ROOT | C–B (C) | Root of the PITCH scale |
| | 2 | SCALE | 12 kinds (Phrygian) | Scale of PITCH |
| | 3 | STEPS | 8 / 16 (8) | Loop length |

The attenuverters set the amount of the internal decay envelope. They act when
TRIG = ON (a trigger patched), and the envelope's length is set by DECAY (as on
the hardware).

### AUX (mixing OUT / AUX)

Plaits has two outputs, OUT and AUX, and on each engine AUX carries a different
sound (the other waveform, a sub-oscillator, another synthesis method etc.).

- 0 % = OUT only, 100 % = AUX only, 50 % = both equally.
- The mix is **equal-power** (OUT × cos, AUX × sin). At 50 % each is added at
  -3 dB, so for two unrelated sounds the overall level barely changes.
- Changes glide over about 2.5 ms, without clicks.

### VELO and TRIG (what a MIDI note does)

A MIDI note behaves as if cables were patched into the hardware like this:

- **Note pitch → V/OCT input** (pitch bend adds ±2 semitones).
- **While the note is held → TRIG input high** (when TRIG = ON). The moment of
  the press is the trigger. When a new note is pressed while the previous one is
  held (legato), or the same note arrives again, the gate drops for one block
  (0.25 ms) before going high again, so **every note triggers**.
  Releasing the later note returns to the pitch of the note still held (no retrigger).
- **VELO = OFF** (default): the LEVEL input is treated as unpatched. The trigger
  "pings" the LPG and it decays over DECAY (the sound of the hardware with only
  TRIG patched). Velocity does not affect the sound (accent is fixed at 0.8, as
  on the hardware).
- **VELO = ON**: the note's velocity is fed into the LEVEL input while the note
  is held. The LPG opens to that voltage (held while pressed, closing according
  to DECAY on release), and velocity also becomes the accent (the strength of the
  drum engines etc.).
- **TRIG = OFF**: the TRIG input is treated as unpatched. The engine sounds
  continuously without triggers. With VELO = ON that makes "a synth that sounds
  only while a note is held"; with VELO = OFF it **drones**, like the hardware
  running free (silent until the first note arrives; to stop it, send CC 120 or
  set TRIG back to ON).
- One voice at a time (monophonic like the hardware Plaits; the latest note
  wins). Plaits is a single-voice instrument by design, so it was not made
  polyphonic.

## MIDI

| Message | Action |
|---|---|
| Note on / off | As above. Note on with velocity 0 is note off |
| Pitch bend | ±2 semitones |
| CC 123 (All Notes Off) | Releases all held notes (sound decays naturally) |
| CC 120 (All Sound Off) | Releases all notes and goes silent within 3 ms (until the next note) |
| MIDI clock | Used as the clock for the automatic triggers only when the host's transport position is unavailable |

## Saved state

The slot state (the set's autosave) and presets hold every parameter (including
ENGINE), the recorded knob movements and the step ON/OFF pattern as text (JSON).
Loading restores everything, including the engine (tests confirm that loading a
saved state into another instance gives the same sound). Unknown keys are
ignored, missing keys keep their value, and out-of-range values are clamped.

## About the sample rate

The Plaits DSP says "48 kHz", but the hardware's I2S clock is
72 MHz ÷ 47 ÷ 32 = **47872.34 Hz**, and the pitch reference (`a0`) is computed
for this real rate (`kCorrectedSampleRate` in `plaits/dsp/dsp.h`). The timing of
decays, the LPG, filters, the trigger delay and so on is all defined in samples
and blocks at this rate.

Rewriting the constants for 44.1 kHz would fix the pitch and break every timing.
So this module runs the DSP at **the hardware's 47872.34 Hz in 12-sample
blocks** and resamples its output to 44.1 kHz. 44100 ÷ 47872.34 is exactly
6909/7500, so output sample positions are computed exactly. The filter is a
64-tap Kaiser-windowed sinc (256 phases, linearly interpolated), flat to 20 kHz
(within ±0.05 dB) and -70 dB at 23.7 kHz. Latency is about 0.7 ms.

As a result **A4 is 440.000 Hz** (measured in the tests), and envelope lengths
and the engines' internal timings match the hardware.

## CPU and memory

- No memory allocation, file access or locks on the audio thread. Instances come
  from a static pool of up to 6, each with its own `plaits::Voice` and the same
  16 KB shared engine RAM as the hardware (about 31 KB per instance).
- All 24 engines fit in 16 KB (the largest, PARTI, is exactly 16384 bytes).
- Switching engines does what the hardware does: load that engine's data and reset.
- **Measured on a Move** (`scripts/bench_move.sh`, 2026-10-13, host 1.7.3): the
  mean per block (128 samples = 2902 µs) is 68–135 µs across all engines; even
  the heaviest, ADDIT, FM and VAVCF, are **about 4.6 %**. The maximum for a block
  that includes an engine switch is 0.8 ms; creating an instance takes 0.1–0.4 ms.
  The benchmark runs at normal priority alongside Move itself, so the per-block
  maxima (0.5–2.5 ms) scatter uniformly regardless of engine weight and include
  preemption by the OS.
- **Measured in a real slot** (Schwung's CPU page, Slot 3, with two other
  instruments and three Master FX running): mean 146 µs (5.7 % of the frame),
  maximum 190 µs.

## Verification

What `scripts/test.sh` (on the host PC) checks:

- **Against the official source**: the bundled source does not differ by a single
  byte from the official source at the recorded commits + `patches/` (downloaded
  and compared every run).
- **Identical output**: for all 24 engines × 3 configurations (standard / VELO ON
  + attenuverters / TRIG OFF + transposed), a performance with notes, legato
  retriggers, releases, knob sweeps and engine switches while playing is played
  through this module and through the official `plaits::Voice` driven directly
  with the hardware's wiring, and every sample of OUT and AUX matches.
- Pitch (A4 = 440.000 Hz, COARSE, FINE, pitch bend) and the resampler's
  frequency response.
- Every engine produces finite, in-range output and stops after a note.
- AUX equal-power mix, VELO, legato retriggers, TRIG OFF, CC 120 / 123.
- One minute of random notes, knobs and engine switches (with AddressSanitizer /
  UndefinedBehaviorSanitizer) gives no crash and no abnormal values.
- Automatic triggers, pads, motion recording, RANDOM, saved state, no hanging notes.
- A smoke test loading the real `dsp.so` through the same API as Schwung, and UI tests.

`scripts/compare_upstream.sh` plays the same performance (all engines, about
2.12 million frames) through **the untouched official source** and the bundled
source and compares them: patches 0001 and 0002 are identical, and the
differences from 0003 and 0004 are only where the official code reads memory it
does not own (with the current performance, only the 60 frames right after
switching to 6-op).

### Patches applied to the official source (`patches/`)

Testing found four out-of-bounds accesses in the official code itself. Each fix
is minimal and marked in the source with an `oPlaits patch` comment.

| Patch | What | Effect on the sound |
|---|---|---|
| 0001 | 6-op FM: with velocity (accent) exactly 1.0, reads `lut_cube_root[17]` (one past the table) | None (the value read is multiplied by 0; the same value is returned without the out-of-bounds read) |
| 0002 | SPEAK: when playing the last phoneme/word frame without interpolation, reads one frame past the table | None (as above) |
| 0003 | WTERR: the AUX output passes a negative phase to the sine function and reads **before** the sine table (up to 153 entries) | In the official code, unrelated data before the table becomes audio at that moment. The negative phase is now wrapped correctly |
| 0004 | 6-op FM: for one block right after an engine switch, reads the accumulation buffer in shared RAM without clearing it | In the official code, 12 samples of the previous engine's leftovers are mixed in. On the hardware (32-bit) the values are nearly harmless, but on the 64-bit Move pointer values (e.g. after WTBL) can come out as huge values or NaN, so it is cleared on switch |

One more: the official WTBL engine writes more pointers (256) into shared RAM than
it allocated (64), but the writes stay inside the 16 KB shared RAM and other
engines reload what they need when switched to, so there is no real harm (the
same on the hardware). This is left unchanged (0004 stops the leftovers of this
write from reaching the sound in 6-op).

### Not yet verified (needs checking on the hardware)

- Sound and operation on a real Move (knobs, touch, pads, jog, screen).
  The UI tests also pass with the Move's own page engine (host 1.7.3) (point
  `OB_SHARED` at a copy of the Move's `shared/`; on WSL `WSLENV=OB_SHARED` is also needed).
- No listening comparison with recordings of a real Plaits (the reference is the
  official source's output).
- Loading user data (custom wavetables, DX7 banks) is not supported.

## Levels and headroom

The Plaits engines come out at the hardware's level (in tests at VOLUME 100 %,
peaks are about -17 to -3 dBFS depending on the engine). VOLUME's default of
50 % is a further -6 dB. A safety soft limiter at the end (rounding smoothly
above -0.9 dBFS) keeps loud sounds at VOLUME 100 % from hard-clipping. At the
default volume the limiter is not reached.

## Licence

- This module's own code (`src/wrapper/`, `src/plaits/` except `mi/`,
  `src/dsp_plugin.cpp`, UI, tests, scripts, `src/phrase/`, `patterns/`): MIT
  (Copyright 2026 Hiro Ikezawa, `LICENSE`).
- `src/plaits/mi/`: Mutable Instruments Plaits and stmlib (by Emilie Gillet,
  Copyright 2012–2021 Emilie Gillet, MIT). Taken from
  https://github.com/pichenettes/eurorack at commit `f4cfa76` (stmlib `e3bd7c9`).
  Details and full texts in `THIRD_PARTY_LICENSES.md`.
- `third_party/schwung/`: Schwung's plugin API header, MIT.

"Mutable Instruments" is a registered trademark. This module is not a Mutable
Instruments product and is not affiliated with the company. It is a port of
published source code under the MIT licence.

## Files

| Path | Contents |
|---|---|
| `src/plaits/mi/` | Official Plaits DSP and stmlib (fetched and verified by `scripts/fetch_plaits.sh`) |
| `src/plaits/plaits_engine.*` | Connects the official `plaits::Voice` to MIDI and Move (in place of the hardware) |
| `src/plaits/resampler.h` | 47872.34 Hz → 44.1 kHz resampler |
| `src/wrapper/` | Automatic triggers, motion recording, scale, RANDOM, saved state, parameter table |
| `src/dsp_plugin.cpp` | Entry point for Schwung plugin API v2 |
| `src/phrase/`, `patterns/` | RANDOM melody generation and source phrases |
| `ui_chain.js`, `ui_core.mjs`, `ui_params.mjs` | Screen, knobs, pads |
| `patches/` | Patches to the official source (the four above) |
| `scripts/` | Build, tests, fetching/verifying the official source, install, CPU measurement on Move |
| `tests/` | Test suite |
