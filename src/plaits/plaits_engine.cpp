// oPlaits - Plaits engine. MIT.
//
// RenderHardwareBlock() is plaits.cc's FillBuffer() for the case that
// matters here -- voice.Render(patch, modulations, output, 12) -- with each
// hardware input replaced by what stands in for it. Nothing inside the voice
// is changed or re-implemented.
#include "plaits/plaits_engine.h"

#include <math.h>
#include <new>
#include <string.h>

#include "stmlib/utils/buffer_allocator.h"
#include "wrapper/params.h"

// The engines are polymorphic (plaits::Engine has pure virtual methods).
// plaits.cc defines this trap itself; so does this module, which links no
// C++ runtime.
extern "C" __attribute__((visibility("hidden"))) void __cxa_pure_virtual() {
  __builtin_trap();
}

namespace oplaits {

static_assert(plaits::kMaxEngines == kNumEngines, "engine range is the voice's");
static_assert(plaits::kBlockSize == 12, "plaits.cc renders 12-sample blocks");

namespace {

// Wrapper output stage, 44.1 kHz.
const float kMuteOpenStep = 1.0f / 44.1f;   // 1 ms linear open
const float kMuteCloseCoef = 0.00753f;      // tau 3 ms (All Sound Off)
const float kLevelCoef = 0.0015f;           // ~15 ms level smoothing
const float kAuxCoef = 0.1f;                // per 12-sample block: ~2.5 ms

}  // namespace

void PlaitsEngine::Init() {
  // As the firmware: zeroed storage, the constructors, then Init() with the
  // 16 KB shared buffer (plaits.cc Init()).
  memset(voice_mem_, 0, sizeof(voice_mem_));
  memset(shared_ram_, 0, sizeof(shared_ram_));
  new (voice_mem_) plaits::Voice();
  stmlib::BufferAllocator allocator(shared_ram_, kSharedRam);
  voice().Init(&allocator);

  memset(&patch_, 0, sizeof(patch_));
  memset(&modulations_, 0, sizeof(modulations_));
  for (int id = 0; id < P_COUNT; ++id) SetParam(id, kParams[id].def);
  resampler_.Reset();
  notes_.Clear();
  note_ = 60.0f;
  bend_ = 0.0f;
  velocity_ = 0.0f;
  retrigger_ = false;
  aux_gain_[0] = 1.0f;
  aux_gain_[1] = 0.0f;
  level_ = s_.level;
  mute_ = 0.0f;
  forced_off_ = true;       // silent until the first note, whatever TRIG says
}

void PlaitsEngine::SetParam(int id, float v) {
  const int i = static_cast<int>(v);
  switch (id) {
    case P_ENGINE: s_.engine = i; break;
    case P_HARMONICS: s_.harmonics = v; break;
    case P_TIMBRE: s_.timbre = v; break;
    case P_MORPH: s_.morph = v; break;
    case P_DECAY: s_.decay = v; break;
    case P_COLOUR: s_.lpg_colour = v; break;
    case P_FM_AMT: s_.fm_amount = v; break;
    case P_TIMBRE_AMT: s_.timbre_amount = v; break;
    case P_MORPH_AMT: s_.morph_amount = v; break;
    case P_AUX_MIX: s_.aux_mix = v; break;
    case P_COARSE: s_.coarse = i; break;
    case P_FINE: s_.fine = i; break;
    case P_VELO: s_.velo = i; break;
    case P_TRIG: s_.trig = i; break;
    case P_LEVEL: s_.level = v; break;
    default: break;
  }
}

int PlaitsEngine::active_engine() const { return voice().active_engine(); }

void PlaitsEngine::NoteOn(uint8_t note, uint8_t velocity) {
  if (!notes_.empty()) retrigger_ = true;
  notes_.Push(note);
  note_ = static_cast<float>(note);
  velocity_ = static_cast<float>(velocity) / 127.0f;
  if (forced_off_) {
    forced_off_ = false;
    // A triggered voice is silent until struck: open at once, so the first
    // attack is not softened. A free-running one fades in.
    if (s_.trig == TRIG_ON) mute_ = 1.0f;
  }
}

// Releasing the newest note with others still held returns to the previous
// one WITHOUT a trigger -- the pitch CV moves, the gate does not.
void PlaitsEngine::NoteOff(uint8_t note) {
  if (!notes_.Remove(note)) return;
  if (!notes_.empty()) note_ = static_cast<float>(notes_.top());
}

void PlaitsEngine::AllNotesOff() {
  notes_.Clear();
  retrigger_ = false;
}

void PlaitsEngine::Silence() {
  AllNotesOff();
  forced_off_ = true;
}

void PlaitsEngine::SetPitchBend(float semitones) { bend_ = semitones; }

void PlaitsEngine::RenderHardwareBlock() {
  // The pots (ui.cc binds them to the Patch). The FREQUENCY pot and its
  // octave switch are replaced by COARSE + FINE in semitones; the V/OCT
  // input (below) carries the MIDI note, so patch.note is the offset.
  patch_.engine = s_.engine;
  patch_.harmonics = s_.harmonics;
  patch_.timbre = s_.timbre;
  patch_.morph = s_.morph;
  patch_.frequency_modulation_amount = s_.fm_amount;
  patch_.timbre_modulation_amount = s_.timbre_amount;
  patch_.morph_modulation_amount = s_.morph_amount;
  patch_.decay = s_.decay;
  patch_.lpg_colour = s_.lpg_colour;
  patch_.note = static_cast<float>(s_.coarse) + static_cast<float>(s_.fine) * 0.01f;

  // The CV inputs. FM, TIMBRE, MORPH, HARMONICS and MODEL are unpatched (0,
  // and the voice uses its internal envelope / defaults for them, as the
  // hardware does when nothing is plugged). V/OCT = the note; TRIG = the
  // gate; LEVEL = the velocity while the key is held.
  const bool held = !notes_.empty();
  modulations_.engine = 0.0f;
  modulations_.note = note_ + bend_;
  modulations_.frequency = 0.0f;
  modulations_.harmonics = 0.0f;
  modulations_.timbre = 0.0f;
  modulations_.morph = 0.0f;
  modulations_.trigger = (held && !retrigger_) ? 1.0f : 0.0f;
  retrigger_ = false;
  modulations_.level = held ? velocity_ : 0.0f;
  modulations_.frequency_patched = false;
  modulations_.timbre_patched = false;
  modulations_.morph_patched = false;
  modulations_.trigger_patched = s_.trig == TRIG_ON;
  modulations_.level_patched = s_.velo == VELO_ON;

  voice().Render(patch_, modulations_, block_, plaits::kBlockSize);
}

void PlaitsEngine::RenderNative(plaits::Voice::Frame *out, int blocks) {
  for (int b = 0; b < blocks; ++b) {
    RenderHardwareBlock();
    memcpy(out + b * plaits::kBlockSize, block_, sizeof(block_));
  }
}

void PlaitsEngine::Render(float *out, size_t frames) {
  const float kScale = -1.0f / 32768.0f;   // the voice writes an inverted signal
  float g_out = 1.0f, g_aux = 0.0f;        // equal-power OUT / AUX crossfade
  if (s_.aux_mix >= 1.0f) {
    g_out = 0.0f;
    g_aux = 1.0f;
  } else if (s_.aux_mix > 0.0f) {
    g_out = cosf(s_.aux_mix * 1.57079633f);
    g_aux = sinf(s_.aux_mix * 1.57079633f);
  }
  for (size_t n = 0; n < frames; ++n) {
    while (resampler_.NeedsInput()) {
      RenderHardwareBlock();
      aux_gain_[0] += (g_out - aux_gain_[0]) * kAuxCoef;
      aux_gain_[1] += (g_aux - aux_gain_[1]) * kAuxCoef;
      if (fabsf(aux_gain_[0] - g_out) < 1e-4f) aux_gain_[0] = g_out;
      if (fabsf(aux_gain_[1] - g_aux) < 1e-4f) aux_gain_[1] = g_aux;
      for (size_t i = 0; i < plaits::kBlockSize; ++i) {
        resampler_.Push((aux_gain_[0] * block_[i].out + aux_gain_[1] * block_[i].aux) * kScale);
      }
    }
    const float x = resampler_.Next();
    if (forced_off_) {
      mute_ += (0.0f - mute_) * kMuteCloseCoef;
      if (mute_ < 1e-5f) mute_ = 0.0f;
    } else if (mute_ < 1.0f) {
      mute_ += kMuteOpenStep;
      if (mute_ > 1.0f) mute_ = 1.0f;
    }
    level_ += (s_.level - level_) * kLevelCoef;
    out[n] = x * mute_ * level_;
  }
}

}  // namespace oplaits
