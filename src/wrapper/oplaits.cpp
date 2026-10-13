// oPlaits - MIDI, the auto 1/16, motion, pads and state. MIT.
#include "wrapper/oplaits.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "wrapper/json_mini.h"
#include "wrapper/scale.h"

namespace oplaits {

namespace {
const int kStateVersion = 1;
const int kClocksPerBeat = 24;
// Automation runs at control rate: every 16 samples (0.36 ms) and exactly on
// each hit.
const int kCtrlFrames = 16;
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int B64Val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

// The parameters the voice itself takes; PITCH, ROOT, SCALE and STEPS are
// the auto trigger's.
bool IsEngineParam(int id) {
  return id != P_PITCH && id != P_ROOT && id != P_SCALE && id != P_STEPS;
}
}  // namespace

void OPlaits::Init(uint32_t seed) {
  plaits_.Init();
  for (int id = 0; id < P_COUNT; ++id) {
    base_[id] = kParams[id].def;
    ovr_[id] = NAN;
    pushed_[id] = NAN;
  }
  PushAll();
  motion_.Init();
  clock_.Init();
  RandomPhrase::LibraryInit();          // once per process; a no-op after
  phrase_.Init(seed);
  gates_ = 0xFFFFu;
  seq_note_ = -1;
  last_pos16_ = -1.0;
  frames_ = 0;
  clk_running_ = clk_awaiting_first_ = false;
  clk_ticks_ = 0;
}

float OPlaits::Effective(int id) const {
  return isnan(ovr_[id]) ? base_[id] : ovr_[id];
}

void OPlaits::Push(int id) {
  if (!IsEngineParam(id)) return;
  const float v = Effective(id);
  if (v == pushed_[id]) return;
  pushed_[id] = v;
  plaits_.SetParam(id, v);
}

void OPlaits::PushAll() {
  for (int id = 0; id < P_COUNT; ++id) Push(id);
}

int OPlaits::PitchNote(float index) const {
  return PitchMidi(static_cast<int>(index + 0.5f), static_cast<int>(base_[P_ROOT]),
                   static_cast<int>(base_[P_SCALE]));
}

// ---- MIDI ---------------------------------------------------------------------

void OPlaits::Midi(const uint8_t *msg, int len) {
  if (!msg || len < 1) return;
  const uint8_t status = msg[0];
  if (status >= 0xF8) {                    // realtime: the fallback clock
    switch (status) {
      case 0xFA: clk_running_ = true; clk_awaiting_first_ = true; clk_ticks_ = 0; break;
      case 0xFB: clk_running_ = true; break;
      case 0xFC: clk_running_ = false; break;
      case 0xF8:
        if (!clk_running_) { clk_running_ = true; clk_awaiting_first_ = true; }
        if (clk_awaiting_first_) { clk_awaiting_first_ = false; clk_ticks_ = 0; }
        else ++clk_ticks_;
        break;
      default: break;
    }
    return;
  }
  if (status >= 0xF0) return;
  if (len < 2 || (msg[1] & 0x80)) return;
  const uint8_t d1 = msg[1];
  const uint8_t d2 = (len >= 3 && !(msg[2] & 0x80)) ? msg[2] : 0;
  switch (status & 0xF0) {
    case 0x90:
      if (len < 3) return;
      if (d2 > 0) plaits_.NoteOn(d1, d2); else plaits_.NoteOff(d1);
      break;
    case 0x80:
      plaits_.NoteOff(d1);
      break;
    case 0xB0:
      if (len < 3) return;
      if (d1 == 120) Panic();
      else if (d1 == 123) { plaits_.AllNotesOff(); seq_note_ = -1; }
      break;
    case 0xE0: {
      if (len < 3) return;
      const int v = static_cast<int>(d1) | (static_cast<int>(d2) << 7);
      plaits_.SetPitchBend((static_cast<float>(v) - 8192.0f) * (2.0f / 8192.0f));
      break;
    }
    default:
      break;
  }
}

void OPlaits::Panic() {
  plaits_.Silence();
  seq_note_ = -1;
}

// ---- the auto 1/16 --------------------------------------------------------------

double OPlaits::FallbackBeat() const {
  if (!clk_running_ || clk_awaiting_first_) return -1.0;
  return static_cast<double>(clk_ticks_) / kClocksPerBeat;
}

void OPlaits::ReleaseSeqNote() {
  if (seq_note_ >= 0) plaits_.NoteOff(static_cast<uint8_t>(seq_note_));
  seq_note_ = -1;
}

// The playhead never steps backwards by less than a 1/16 (that is host
// jitter, not a new Play).
void OPlaits::MotionAt(double pos16) {
  if (pos16 >= 0.0 && last_pos16_ >= 0.0 && pos16 < last_pos16_ && pos16 > last_pos16_ - 1.0)
    pos16 = last_pos16_;
  last_pos16_ = pos16;
  motion_.Update(MotionParams(), pos16, steps());
  PushAll();
}

// A trigger step: the note PITCH plays at the start of this 1/16. The new
// note goes on before the old one comes off, so the voice sees a legato
// retrigger, never a gap; a step whose pad is off releases the note.
void OPlaits::Hit(int64_t abs16) {
  const int len = steps();
  const int step = static_cast<int>(((abs16 % len) + len) % len);
  if (!((gates_ >> step) & 1u)) {
    ReleaseSeqNote();
    return;
  }
  const int note = PitchNote(Effective(P_PITCH));
  plaits_.NoteOn(static_cast<uint8_t>(note), 100);
  if (seq_note_ >= 0 && seq_note_ != note) plaits_.NoteOff(static_cast<uint8_t>(seq_note_));
  seq_note_ = note;
}

void OPlaits::Process(float *out, size_t frames, double beat, float bpm, bool host_has_transport) {
  const int n = static_cast<int>(frames);
  if (!host_has_transport) beat = FallbackBeat();
  if (!(bpm >= 20.0f && bpm <= 999.0f)) bpm = 120.0f;
  const double spb = 44100.0 * 60.0 / bpm;
  frames_ += static_cast<uint32_t>(n);
  motion_.Age(n);

  const bool was_running = clock_.running;
  int offsets[Clock::kMaxEvents];
  const int events = clock_.Advance(beat, bpm, n, offsets);
  if (!clock_.running) {
    if (was_running) ReleaseSeqNote();
    last_pos16_ = -1.0;
    MotionAt(-1.0);
  } else if (!was_running) {
    last_pos16_ = -1.0;                  // a new Play
  }

  // Split the block at each 1/16 so a hit lands on its sample, and step the
  // automation to the hit's exact 1/16 first, so PITCH and the sound are the
  // values at the start of that step.
  int pos = 0;
  auto span = [&](int to) {
    while (pos < to) {
      int m = to - pos;
      if (m > kCtrlFrames) m = kCtrlFrames;
      if (clock_.running) MotionAt((beat + pos / spb) * 4.0);
      plaits_.Render(out + pos, static_cast<size_t>(m));
      pos += m;
    }
  };
  for (int k = 0; k < events; ++k) {
    span(offsets[k]);
    const int64_t abs16 = clock_.last_abs - (events - 1 - k);
    if (last_pos16_ > static_cast<double>(abs16)) last_pos16_ = -1.0;   // restart
    MotionAt(static_cast<double>(abs16));
    Hit(abs16);
  }
  span(n);
}

// ---- parameters and pads ----------------------------------------------------------

bool OPlaits::SetParam(int id, float value) {
  if (!SanitizeParam(id, &value)) return false;
  if (id == P_ROOT || id == P_SCALE) { SetKey(id, value); return true; }
  if (id == P_PITCH) {
    const float top = static_cast<float>(PitchCount(static_cast<int>(base_[P_ROOT]),
                                                    static_cast<int>(base_[P_SCALE])) - 1);
    if (value > top) value = top;
  }
  base_[id] = value;
  Push(id);
  return true;
}

// A new root or scale re-snaps PITCH -- the knob, what is playing, and every
// recorded pitch point -- to the nearest note of the new scale, so the voice
// does not jump to whatever note now sits at the old index.
void OPlaits::SetKey(int id, float value) {
  const int r0 = static_cast<int>(base_[P_ROOT]), s0 = static_cast<int>(base_[P_SCALE]);
  base_[id] = value;
  const int r1 = static_cast<int>(base_[P_ROOT]), s1 = static_cast<int>(base_[P_SCALE]);
  if (r0 == r1 && s0 == s1) return;
  auto resnap = [&](float idx) {
    return static_cast<float>(PitchIndexOfMidi(
        static_cast<float>(PitchMidi(static_cast<int>(idx + 0.5f), r0, s0)), r1, s1));
  };
  base_[P_PITCH] = resnap(base_[P_PITCH]);
  if (!isnan(ovr_[P_PITCH])) ovr_[P_PITCH] = resnap(ovr_[P_PITCH]);
  uint16_t *lane = motion_.lane(Motion::LaneOf(P_PITCH));
  for (int i = 0; i < Motion::kPoints; ++i) {
    if (lane[i] == Motion::kEmpty) continue;
    const float x = Motion::Denorm(P_PITCH, Motion::Dq(lane[i]));
    lane[i] = Motion::Q(Motion::Norm(P_PITCH, resnap(x)));
  }
}

void OPlaits::ToggleGate(int step) {
  if (step >= 0 && step < 16) gates_ ^= static_cast<uint16_t>(1u << step);
}

void OPlaits::Touch(int param_id, bool down) {
  const int k = Motion::LaneOf(param_id);
  if (k < 0) return;
  motion_.Touch(MotionParams(), k, down);
  Push(param_id);
}

void OPlaits::ClearMotion(int param_id) {
  const int k = param_id < 0 ? -1 : Motion::LaneOf(param_id);
  if (param_id >= 0 && k < 0) return;
  motion_.Clear(MotionParams(), k);
  PushAll();
}

// The RANDOM pad: a new Phrase Gen phrase, written as PITCH automation --
// each note set as a knob value of the scale in use (nearest note, moved to
// ROOT), never quantized afterwards. Phrase Gen's sources span ~6 octaves,
// so each note is folded by octaves into a window around the knob's own
// note (a fifth below to 17 semitones above): the melody keeps its note
// names, knob 8 sets its register. Every point of a step holds its note.
void OPlaits::Randomize() {
  const int len = steps();
  int notes[16];
  if (!phrase_.Generate(len, frames_, notes)) return;
  const int r = static_cast<int>(base_[P_ROOT]), sc = static_cast<int>(base_[P_SCALE]);
  const int knob = PitchNote(base_[P_PITCH]);
  int lo = knob - 7, hi = knob + 17;
  if (lo < kPitchLo) { hi += kPitchLo - lo; lo = kPitchLo; }
  if (hi > kPitchHi) { lo -= hi - kPitchHi; hi = kPitchHi; }
  uint16_t *lane = motion_.lane(Motion::LaneOf(P_PITCH));
  for (int st = 0; st < len; ++st) {
    int n = notes[st];
    if (n < 0) n = knob - r;
    n += r;                                   // the phrases are in C
    while (n > hi) n -= 12;
    while (n < lo) n += 12;
    while (n > kPitchHi) n -= 12;
    while (n < kPitchLo) n += 12;
    const uint16_t q = Motion::Q(Motion::Norm(
        P_PITCH, static_cast<float>(PitchIndexOfMidi(static_cast<float>(n), r, sc))));
    for (int j = 0; j < Motion::kSub; ++j) lane[st * Motion::kSub + j] = q;
  }
}

// ---- state ----------------------------------------------------------------------
//
// {"v":1,"level":0.5000,"engine":8,"harmonics":0.5000,...,"pitch":14,...,
//  "m":"<key>:<data>;...","g":65535}
// Integers (ENGINE, PITCH, COARSE, ...) are written as integers. "m" carries
// only lanes that hold automation: Motion::kPoints 12-bit values normalized
// over the parameter's range, two base64url characters each (4095 = not
// written); "g" the trigger pads. Restore applies every key it recognises,
// clamps, and leaves missing keys alone, so a preset written before a
// parameter existed still loads.

int OPlaits::WriteState(char *buf, int size) const {
  if (!buf || size <= 0) return -1;
  int n = snprintf(buf, size, "{\"v\":%d", kStateVersion);
  for (int id = 0; id < P_COUNT && n > 0 && n < size; ++id) {
    const ParamInfo &p = kParams[id];
    if (p.type == PT_FLOAT)
      n += snprintf(buf + n, size - n, ",\"%s\":%.4f", p.key, base_[id]);
    else
      n += snprintf(buf + n, size - n, ",\"%s\":%d", p.key, static_cast<int>(lroundf(base_[id])));
  }
  if (n > 0 && n < size) n += snprintf(buf + n, size - n, ",\"m\":\"");
  const unsigned mask = motion_.Mask();
  bool first = true;
  for (int k = 0; k < Motion::kLanes && n > 0 && n < size; ++k) {
    if (!(mask & (1u << k))) continue;
    n += snprintf(buf + n, size - n, "%s%s:", first ? "" : ";", kParams[Motion::kParam[k]].key);
    first = false;
    if (n < 0 || n + 2 * Motion::kPoints >= size) { buf[0] = 0; return -1; }
    const uint16_t *lane = motion_.lane(k);
    for (int i = 0; i < Motion::kPoints; ++i) {
      const int q = lane[i] == Motion::kEmpty ? 4095 : lane[i];
      buf[n++] = kB64[q >> 6];
      buf[n++] = kB64[q & 63];
    }
    buf[n] = 0;
  }
  if (n > 0 && n < size) n += snprintf(buf + n, size - n, "\",\"g\":%u}", gates_);
  if (n < 0 || n >= size) {
    buf[0] = 0;
    return -1;
  }
  return n;
}

bool OPlaits::ReadState(const char *text) {
  if (!text) return false;
  const size_t len = strnlen(text, 65536);
  if (len == 0 || len >= 65536) return false;
  const char *end = text + len;
  const char *p = json::SkipWs(text, end);
  if (p >= end || *p != '{') return false;
  const char *root_end = json::ContainerEnd(p, end);
  if (!root_end) return false;

  float v = 1.0f;
  const char *vp = json::FindMember(p, root_end, "v");
  if (vp && (!json::ReadNumber(vp, root_end, &v) || v != 1.0f)) return false;

  // Motion first, into scratch: a malformed lane refuses the whole "m".
  static uint16_t scratch[Motion::kLanes][Motion::kPoints];   // one SPI thread only
  bool have_m = false;
  const char *mp = json::FindMember(p, root_end, "m");
  if (mp && *mp == '"') {
    for (int k = 0; k < Motion::kLanes; ++k)
      for (int i = 0; i < Motion::kPoints; ++i) scratch[k][i] = Motion::kEmpty;
    const char *q = mp + 1;
    have_m = true;
    unsigned seen = 0;
    while (q < root_end && *q != '"') {
      const char *colon = static_cast<const char *>(memchr(q, ':', root_end - q));
      if (!colon || colon - q >= 24) { have_m = false; break; }
      char key[24];
      memcpy(key, q, colon - q);
      key[colon - q] = 0;
      const int id = FindParam(key);
      const int k = Motion::LaneOf(id);
      if (id < 0 || colon + 1 + 2 * Motion::kPoints > root_end) { have_m = false; break; }
      if (k >= 0 && (seen & (1u << k))) { have_m = false; break; }
      q = colon + 1;
      if (k < 0) {
        // A parameter that is not recorded (e.g. a lane from a later
        // version): its lane is dropped, and the others are kept.
        q += 2 * Motion::kPoints;
        if (*q == ';') ++q;
        else if (*q != '"') { have_m = false; break; }
        continue;
      }
      seen |= 1u << k;
      for (int i = 0; i < Motion::kPoints; ++i) {
        const int hi = B64Val(q[0]), lo = hi < 0 ? -1 : B64Val(q[1]);
        if (hi < 0 || lo < 0) { have_m = false; break; }
        const int x = hi * 64 + lo;
        scratch[k][i] = x == 4095 ? Motion::kEmpty
                                  : static_cast<uint16_t>(x > Motion::kQMax ? Motion::kQMax : x);
        q += 2;
      }
      if (!have_m) break;
      if (*q == ';') ++q;
      else if (*q != '"') { have_m = false; break; }
    }
  }

  // Root and scale first: PITCH is clamped to the scale in force.
  float f;
  static const int kKey[2] = {P_ROOT, P_SCALE};
  for (int id : kKey) {
    const char *val = json::FindMember(p, root_end, kParams[id].key);
    if (val && json::ReadNumber(val, root_end, &f) && SanitizeParam(id, &f)) base_[id] = f;
  }
  for (int id = 0; id < P_COUNT; ++id) {
    if (id == P_ROOT || id == P_SCALE) continue;
    const char *val = json::FindMember(p, root_end, kParams[id].key);
    if (val && json::ReadNumber(val, root_end, &f)) SetParam(id, f);
  }
  const char *gp = json::FindMember(p, root_end, "g");
  if (gp && json::ReadNumber(gp, root_end, &f) && f == floorf(f) && f >= 0.0f && f <= 65535.0f)
    gates_ = static_cast<uint16_t>(f);
  if (have_m) {
    motion_.Clear(MotionParams(), -1);
    for (int k = 0; k < Motion::kLanes; ++k)
      memcpy(motion_.lane(k), scratch[k], sizeof(scratch[k]));
  }
  PushAll();
  return true;
}

}  // namespace oplaits
