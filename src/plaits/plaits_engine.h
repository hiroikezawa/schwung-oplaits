// oPlaits - Plaits engine. MIT.
//
// The DSP is Mutable Instruments' own Plaits code (src/plaits/mi/, vendored
// unmodified from pichenettes/eurorack, scripts/fetch_plaits.sh): the whole
// plaits::Voice -- engine selection, the 24 engines, the internal decay
// envelope, the low-pass gate and the output limiter -- is called exactly as
// the firmware's plaits.cc calls it, one 12-sample block at a time, with the
// same 16 KB of shared engine RAM. This class is what stands in for the
// hardware AROUND the voice: the pots become parameters, the TRIG / LEVEL /
// V/OCT inputs become MIDI, and the DAC becomes a resampler.
//
// SAMPLE RATE. The voice is not "a 48 kHz oscillator": its pitch constant a0
// is computed against the hardware's real I2S frame rate, 47872.34 Hz
// (plaits/dsp/dsp.h, kCorrectedSampleRate), and every decay, LPG and filter
// time is a count of samples or blocks at that rate. Rescaling for 44.1 kHz
// would fix one of those and leave the rest wrong. So the voice runs EXACTLY
// as the hardware does -- 47872.34 Hz, 12-sample blocks -- and a polyphase
// FIR takes the result to 44.1 kHz (resampler.h). Pitch, envelopes and every
// engine's internal timing are then the hardware's by construction.
#ifndef OPLAITS_PLAITS_ENGINE_H_
#define OPLAITS_PLAITS_ENGINE_H_

#include <stddef.h>
#include <stdint.h>

#include "plaits/dsp/voice.h"
#include "plaits/resampler.h"
#include "wrapper/note_stack.h"

namespace oplaits {

const int kNumEngines = 24;
// plaits.cc: char shared_buffer[16384], shared by all the engines.
const size_t kSharedRam = 16384;

// Plaits' own controls in the hardware's own units, plus the wrapper's.
struct PlaitsSettings {
  int engine;                // 0..23, plaits::Patch::engine
  float harmonics;           // HARMONICS pot 0..1
  float timbre;              // TIMBRE pot 0..1
  float morph;               // MORPH pot 0..1
  float decay;               // DECAY (MORPH's shift function) 0..1
  float lpg_colour;          // LPG COLOUR (TIMBRE's shift function) 0..1
  float fm_amount;           // FM attenuverter -1..1
  float timbre_amount;       // TIMBRE attenuverter -1..1
  float morph_amount;        // MORPH attenuverter -1..1
  // Wrapper-level (not Plaits controls):
  float aux_mix;             // 0 = OUT, 1 = AUX
  int coarse;                // semitones -24..24
  int fine;                  // cents -100..100
  int velo;                  // Velo
  int trig;                  // Trig
  float level;               // output level 0..1
};

class PlaitsEngine {
 public:
  void Init();

  const PlaitsSettings &settings() const { return s_; }
  // id is a ParamId; value already sanitized.
  void SetParam(int id, float value);

  // A note-on is a rising edge at TRIG with the note's pitch at V/OCT (and,
  // with VELO on, its velocity at LEVEL); the gate stays high while any key
  // is held. A note-on while the gate is already high drops it for one block
  // first, so every note-on is a new trigger.
  void NoteOn(uint8_t note, uint8_t velocity);
  void NoteOff(uint8_t note);
  void AllNotesOff();
  // All Sound Off: releases every key and, after the voice's own release,
  // keeps the output shut (a 3 ms ramp) until the next note-on -- the only
  // way to silence TRIG OFF + VELO OFF, which drones by design.
  void Silence();
  void SetPitchBend(float semitones);

  // Mono, 44.1 kHz.
  void Render(float *out, size_t frames);

  bool key_held() const { return !notes_.empty(); }
  int active_engine() const;

  // Measurement only (tests/test_engine.cpp): `blocks` hardware blocks at
  // the voice's own rate, exactly as Render() would feed the resampler,
  // returned as the voice wrote them (OUT and AUX, int16, inverted).
  void RenderNative(plaits::Voice::Frame *out, int blocks);

 private:
  void RenderHardwareBlock();   // one plaits.cc FillBuffer(): 12 samples

  plaits::Voice &voice() { return *reinterpret_cast<plaits::Voice *>(voice_mem_); }
  const plaits::Voice &voice() const { return *reinterpret_cast<const plaits::Voice *>(voice_mem_); }

  PlaitsSettings s_;

  // The firmware's Voice is a global, i.e. zero-initialised before its
  // constructor and Init() run. Held as raw storage so a reused pool slot
  // can be given exactly that again (Init: memset + placement new).
  alignas(plaits::Voice) unsigned char voice_mem_[sizeof(plaits::Voice)];
  alignas(16) char shared_ram_[kSharedRam];
  plaits::Patch patch_;
  plaits::Modulations modulations_;
  plaits::Voice::Frame block_[plaits::kBlockSize];
  Resampler resampler_;

  NoteStack notes_;
  float note_;               // MIDI note held at V/OCT
  float bend_;               // semitones
  float velocity_;           // 0..1, the LEVEL input with VELO on
  bool retrigger_;           // drop the gate for the next block
  float aux_gain_[2];        // OUT / AUX gains, smoothed per block
  float level_;              // smoothed s_.level
  float mute_;               // Silence(): 0 = shut
  bool forced_off_;
};

}  // namespace oplaits

#endif  // OPLAITS_PLAITS_ENGINE_H_
