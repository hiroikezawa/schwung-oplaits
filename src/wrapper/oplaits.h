// oPlaits - the Plaits voice played on every 1/16 of Move's transport, with a
// performance layer: 16 trigger gates, touch-recorded knob motion,
// a RANDOM pad that writes a Phrase Gen phrase into PITCH, and a PITCH knob
// that IS a note of the scale. MIT.
#ifndef OPLAITS_OPLAITS_H_
#define OPLAITS_OPLAITS_H_

#include <stddef.h>
#include <stdint.h>

#include "plaits/plaits_engine.h"
#include "wrapper/motion.h"
#include "wrapper/params.h"
#include "wrapper/random_phrase.h"

namespace oplaits {

class OPlaits {
 public:
  void Init(uint32_t seed);

  // MIDI: notes play the voice directly (velocity-0 note-on is a note-off),
  // CC 120 / 123, pitch bend +-2 semitones, and MIDI clock (used only when
  // the host has no transport position).
  void Midi(const uint8_t *msg, int len);

  // Mono, 44.1 kHz. `beat` = host transport position in beats (< 0 stopped).
  void Process(float *out, size_t frames, double beat, float bpm, bool host_has_transport);

  bool SetParam(int id, float value);           // a knob: the BASE value
  float GetParam(int id) const { return base_[id]; }
  float Effective(int id) const;                // what is playing (automation)

  // The pads and the knobs' touch sensors.
  void SetWrite(bool on) { motion_.set_write(on); }
  bool write() const { return motion_.write(); }
  void ToggleGate(int step);
  uint16_t gates() const { return gates_; }
  void Randomize();
  void Touch(int param_id, bool down);
  void ClearMotion(int param_id);               // -1 = all

  int steps() const { return base_[P_STEPS] >= 0.5f ? 16 : 8; }
  unsigned motion_mask() const { return motion_.Mask(); }
  unsigned motion_recording() const { return motion_.Recording(); }
  int motion_step() const { return motion_.step(); }

  // State (see oplaits.cpp for the format).
  int WriteState(char *buf, int size) const;
  bool ReadState(const char *text);

  void Panic();
  bool key_held() const { return plaits_.key_held(); }
  int active_engine() const { return plaits_.active_engine(); }

 private:
  Motion::Params MotionParams() { return Motion::Params{base_, ovr_}; }
  void Push(int id);                            // effective value -> engine
  void PushAll();
  void SetKey(int id, float value);             // ROOT / SCALE: re-snap PITCH
  void MotionAt(double pos16);
  void Hit(int64_t abs16);
  void ReleaseSeqNote();
  int PitchNote(float index) const;
  double FallbackBeat() const;

  PlaitsEngine plaits_;
  float base_[P_COUNT];
  float ovr_[P_COUNT];            // motion playback, NAN = none
  float pushed_[P_COUNT];         // what the engine has, to push only changes
  Motion motion_;
  Clock clock_;
  RandomPhrase phrase_;
  uint16_t gates_;                // bit n = step n+1 of 16 fires
  int seq_note_;                  // the note the auto trigger holds, -1 none
  double last_pos16_;             // monotonic playhead (host beat jitter)
  uint32_t frames_;               // since create: entropy for RANDOM
  bool clk_running_, clk_awaiting_first_;
  long long clk_ticks_;
};

}  // namespace oplaits

#endif  // OPLAITS_OPLAITS_H_
