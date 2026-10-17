// oPlaits - parameter table. MIT.
#include "wrapper/params.h"

#include <math.h>
#include <string.h>

namespace oplaits {

// Ranges and defaults are the hardware's (plaits/ui.cc, plaits/settings.cc):
// ENGINE 0..23 with 8 (the first green model, VA) the factory default; DECAY
// 0.5 and LPG COLOUR 0 the factory state; the pots at noon and the three
// attenuverters at their centre detent (0 = no modulation). VOLUME, AUX MIX,
// COARSE / FINE (the FREQUENCY pot's job, done in semitones here), VELO and
// TRIG are the wrapper's (params.h). PITCH is a scale-note index (scale.h):
// its table max is the chromatic top (60); the served chain_params carry the
// current scale's. 14 = C3 in C Phrygian.
//
// FORMAT IS PARSED by scripts/metadata.py: one row per line, in this order.
const ParamInfo kParams[P_COUNT] = {
  {"level",      PT_FLOAT,   0.0f,    1.0f,  0.5f},
  {"harmonics",  PT_FLOAT,   0.0f,    1.0f,  0.5f},
  {"timbre",     PT_FLOAT,   0.0f,    1.0f,  0.5f},
  {"decay",      PT_FLOAT,   0.0f,    1.0f,  0.5f},
  {"colour",     PT_FLOAT,   0.0f,    1.0f,  0.0f},
  {"morph",      PT_FLOAT,   0.0f,    1.0f,  0.5f},
  {"engine",     PT_ENUM,    0.0f,   23.0f,  8.0f},
  {"pitch",      PT_INT,     0.0f,   60.0f, 14.0f},
  {"fm_amt",     PT_FLOAT,  -1.0f,    1.0f,  0.0f},
  {"timbre_amt", PT_FLOAT,  -1.0f,    1.0f,  0.0f},
  {"morph_amt",  PT_FLOAT,  -1.0f,    1.0f,  0.0f},
  {"aux_mix",    PT_FLOAT,   0.0f,    1.0f,  0.0f},
  {"coarse",     PT_INT,   -24.0f,   24.0f,  0.0f},
  {"fine",       PT_INT,  -100.0f,  100.0f,  0.0f},
  {"velo",       PT_ENUM,    0.0f,    1.0f,  0.0f},
  {"trig",       PT_ENUM,    0.0f,    1.0f,  0.0f},
  {"root",       PT_ENUM,    0.0f,   11.0f,  0.0f},
  {"scale",      PT_ENUM,    0.0f,   11.0f,  0.0f},
  {"steps",      PT_ENUM,    0.0f,    1.0f,  0.0f},
};

int FindParam(const char *key) {
  if (!key) return -1;
  for (int i = 0; i < P_COUNT; ++i) {
    if (!strcmp(key, kParams[i].key)) return i;
  }
  return -1;
}

bool SanitizeParam(int id, float *value) {
  if (id < 0 || id >= P_COUNT || !value || !isfinite(*value)) return false;
  const ParamInfo &p = kParams[id];
  float v = *value;
  if (p.type != PT_FLOAT) v = floorf(v + 0.5f);
  if (v < p.min) v = p.min;
  if (v > p.max) v = p.max;
  *value = v;
  return true;
}

}  // namespace oplaits
