// oPlaits - parameter table. MIT.
//
// METADATA ONLY: the values live in PlaitsEngine (the official Patch). This
// table names each key, its range and its default. module.json's
// chain_params is cross-checked against it at build time
// (scripts/metadata.py), so the host's idea of a key can never drift from
// what the DSP does with it.
#ifndef OPLAITS_PARAMS_H_
#define OPLAITS_PARAMS_H_

#include <stdint.h>

namespace oplaits {

enum ParamType { PT_FLOAT = 0, PT_INT = 1, PT_ENUM = 2 };

struct ParamInfo {
  const char *key;   // set_param / get_param key, and the key in the state blob
  uint8_t type;
  float min, max, def;
};

// In the knob grid's page order (module.json's chain_params must match):
//   main    VOLUME HARMONICS TIMBRE DECAY COLOR MORPH ENGINE PITCH
//   Plaits  FM> TIMB> MORPH> AUX-MIX COARSE FINE VELO TRIG
//   Scale   ROOT SCALE STEPS
// Plaits' own controls use the hardware's own ranges: the panel pots 0..1,
// the three attenuverters -1..+1, DECAY / LPG COLOUR 0..1.
enum ParamId {
  P_LEVEL, P_HARMONICS, P_TIMBRE, P_DECAY, P_COLOUR, P_MORPH, P_ENGINE, P_PITCH,
  P_FM_AMT, P_TIMBRE_AMT, P_MORPH_AMT, P_AUX_MIX, P_COARSE, P_FINE, P_VELO, P_TRIG,
  P_ROOT, P_SCALE, P_STEPS,
  P_COUNT
};

extern const ParamInfo kParams[P_COUNT];

int FindParam(const char *key);

// Clamp to range; integer types are also rounded. Returns false for NaN/Inf.
bool SanitizeParam(int id, float *value);

// VELO: what the LEVEL input carries.
//   OFF  LEVEL unpatched: the trigger pings the low-pass gate, accent 0.8
//        (Plaits with only TRIG patched).
//   ON   LEVEL patched with the note's velocity while the key is held: the
//        low-pass gate follows it and the velocity is the accent.
enum Velo { VELO_OFF = 0, VELO_ON = 1 };
// TRIG: whether the TRIG input is patched.
//   ON   a note-on is a trigger (gate high while the key is held).
//   OFF  TRIG unpatched: the engines run free (with VELO OFF that is the
//        hardware's free-running drone).
enum Trig { TRIG_ON = 0, TRIG_OFF = 1 };

}  // namespace oplaits

#endif  // OPLAITS_PARAMS_H_
