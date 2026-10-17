// oPlaits - Schwung plugin API v2 adapter. MIT.
//
// EVERY entry point here runs on the SPI audio callback (plugin_api_v1.h):
// no allocation (instances come from a static pool), no file I/O, no
// logging, bounded work.

// plugin_api_v1.h is a C header and asserts its own layout with C11
// _Static_assert, which C++ spells static_assert. Same check, same header,
// unmodified.
#define _Static_assert static_assert
extern "C" {
#include "host/plugin_api_v1.h"
}
#undef _Static_assert

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include "oplaits_metadata.h"
#include "wrapper/oplaits.h"
#include "wrapper/scale.h"

using oplaits::OPlaits;

namespace {

// Four chain slots can each hold one; two spare cover a swap in flight.
// Each instance is one plaits::Voice with its own 16 KB of engine RAM.
const int kInstances = 6;

struct Instance {
  std::atomic<int> used;
  OPlaits engine;
  float mono[MOVE_FRAMES_PER_BLOCK];
};

Instance g_pool[kInstances];
const host_api_v1_t *g_host = NULL;
std::atomic<unsigned> g_seed{0x2545F491u};

static_assert(std::atomic<int>::is_always_lock_free, "pool needs lock-free atomics");

// The shim parks a slot whose output stays below 5 LSB for ~1 s and renders
// it only every ~0.5 s after that (schwung_shim.c, DSP_IDLE_THRESHOLD), so the
// first hits after Play would be late. A block that would be silent carries a
// 6 LSB DC offset (-75 dBFS, inaudible, removed by any DC blocker) -- except
// at LEVEL 0, which is exact digital silence.
const int kKeepaliveLsb = 6;
const int kSilenceLsb = 4;

// The knob grid is Schwung's own page engine, driven from ui_chain.js so the
// module sees the knobs' touch sensors. Served as `grid_hierarchy`, while
// `ui_hierarchy` is served EMPTY on purpose: a hierarchy there makes the
// shadow UI draw its stock grid, which runs no module code and so never sees
// a knob touch.
const char kHierarchy[] =
    "{\"modes\":null,\"levels\":{"
      "\"root\":{\"label\":\"oPlaits\","
        "\"knobs\":[\"level\",\"harmonics\",\"timbre\",\"decay\",\"colour\",\"morph\",\"engine\",\"pitch\"],"
        "\"params\":[\"level\",\"harmonics\",\"timbre\",\"decay\",\"colour\",\"morph\",\"engine\",\"pitch\","
          "{\"level\":\"plaits\",\"label\":\"Plaits\"},"
          "{\"level\":\"scale\",\"label\":\"Scale\"}]},"
      "\"plaits\":{\"label\":\"Plaits\","
        "\"knobs\":[\"fm_amt\",\"timbre_amt\",\"morph_amt\",\"aux_mix\",\"coarse\",\"fine\",\"velo\",\"trig\"],"
        "\"params\":[\"fm_amt\",\"timbre_amt\",\"morph_amt\",\"aux_mix\",\"coarse\",\"fine\",\"velo\",\"trig\"]},"
      "\"scale\":{\"label\":\"Scale\","
        "\"knobs\":[\"root\",\"scale\",\"steps\"],"
        "\"params\":[\"root\",\"scale\",\"steps\"]}"
    "}}";

int CopyText(char *buf, int size, const char *text) {
  const size_t n = strlen(text);
  if (n >= static_cast<size_t>(size)) { buf[0] = 0; return -1; }
  memcpy(buf, text, n + 1);
  return static_cast<int>(n);
}

const char *SkipWs(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
  return p;
}

// A whole-string number; NaN/Inf and trailing junk are refused.
bool ParseFloat(const char *s, float *out) {
  char *end;
  const float v = strtof(s, &end);
  if (end == s || *SkipWs(end) || !isfinite(v)) return false;
  *out = v;
  return true;
}

// Output protection. Linear up to 0.9 (-0.9 dBFS), then a tanh-shaped knee
// that meets full scale asymptotically, so a hot engine at VOLUME 100 rounds
// off instead of hard-clipping. At the default level nothing reaches the
// knee (README). NaN becomes silence. This is the wrapper's own output
// protection after the voice; Plaits' own limiter (for the engines that ask
// for one) runs inside the voice, unchanged.
float SoftLimit(float x) {
  if (!(x == x)) return 0.0f;
  const float kKnee = 0.9f;
  const float a = fabsf(x);
  if (a <= kKnee) return x;
  float z = (a - kKnee) * (1.0f / (1.0f - kKnee));
  z = z >= 3.0f ? 1.0f : z * (27.0f + z * z) / (27.0f + 9.0f * z * z);
  const float y = kKnee + (1.0f - kKnee) * z;
  return x < 0.0f ? -y : y;
}

int Finish(char *buf, int size, int n) {
  if (n < 0 || n >= size) {
    buf[0] = 0;
    return -1;
  }
  return n;
}

int FormatValue(int id, float v, char *buf, int size) {
  if (oplaits::kParams[id].type == oplaits::PT_FLOAT) return snprintf(buf, size, "%.4f", v);
  return snprintf(buf, size, "%d", static_cast<int>(lroundf(v)));
}

int FormatParam(const OPlaits &e, int id, char *buf, int size) {
  const float v = e.GetParam(id);
  if (oplaits::kParams[id].type == oplaits::PT_FLOAT) return snprintf(buf, size, "%.4f", v);
  return snprintf(buf, size, "%d", static_cast<int>(lroundf(v)));
}

void *create_instance(const char *module_dir, const char *json_defaults) {
  (void)module_dir;
  (void)json_defaults;
  for (int i = 0; i < kInstances; ++i) {
    int expected = 0;
    if (g_pool[i].used.compare_exchange_strong(expected, 1)) {
      // A different seed per instance, so two slots never write the same phrases.
      // Init() builds the voice exactly as plaits.cc does: bounded, no
      // allocation (the engines' RAM is the instance's own 16 KB).
      g_pool[i].engine.Init(g_seed.fetch_add(0x9E3779B9u) ^
                            static_cast<unsigned>(reinterpret_cast<uintptr_t>(&g_pool[i])));
      return &g_pool[i];
    }
  }
  return NULL;
}

void destroy_instance(void *ptr) {
  if (!ptr) return;
  static_cast<Instance *>(ptr)->used.store(0);
}

void on_midi(void *ptr, const uint8_t *msg, int len, int source) {
  (void)source;
  if (!ptr || !msg) return;
  static_cast<Instance *>(ptr)->engine.Midi(msg, len);
}

void set_param(void *ptr, const char *key, const char *val) {
  if (!ptr || !key || !val) return;
  OPlaits &e = static_cast<Instance *>(ptr)->engine;
  if (!strcmp(key, "state")) {
    (void)e.ReadState(val);
    return;
  }
  float f;
  // The pads (ui_core.mjs): REC arms motion recording, the 16 trigger steps,
  // RANDOM writes a Phrase Gen phrase into PITCH.
  if (!strcmp(key, "write")) {
    if (!strcmp(val, "0") || !strcmp(val, "1")) e.SetWrite(val[0] == '1');
    return;
  }
  if (!strcmp(key, "gate_toggle")) {
    if (ParseFloat(val, &f) && f == floorf(f) && f >= 0.0f && f < 16.0f) e.ToggleGate(static_cast<int>(f));
    return;
  }
  if (!strcmp(key, "randomize")) {
    e.Randomize();
    return;
  }
  // "touch" = "<key>,<0|1>": a knob's capacitive sensor. "motion_clear" =
  // "<key>" or "all".
  if (!strcmp(key, "touch")) {
    const char *comma = strchr(val, ',');
    char k[32];
    if (!comma || static_cast<size_t>(comma - val) >= sizeof(k)) return;
    memcpy(k, val, comma - val);
    k[comma - val] = 0;
    const int id = oplaits::FindParam(k);
    if (id < 0 || (strcmp(comma + 1, "0") && strcmp(comma + 1, "1"))) return;
    e.Touch(id, comma[1] == '1');
    return;
  }
  if (!strcmp(key, "motion_clear")) {
    if (!strcmp(val, "all")) e.ClearMotion(-1);
    else if (oplaits::FindParam(val) >= 0) e.ClearMotion(oplaits::FindParam(val));
    return;
  }
  if (!strcmp(key, "panic")) {
    e.Panic();
    return;
  }
  const int id = oplaits::FindParam(key);
  if (id >= 0 && ParseFloat(val, &f)) (void)e.SetParam(id, f);
}

int get_param(void *ptr, const char *key, char *buf, int size) {
  if (!ptr || !key || !buf || size <= 0) return -1;
  buf[0] = 0;
  Instance *inst = static_cast<Instance *>(ptr);
  const int id = oplaits::FindParam(key);
  const OPlaits &e = inst->engine;
  if (id >= 0) return Finish(buf, size, FormatParam(e, id, buf, size));
  // eff_<key>: what the sound is using right now (automation playback).
  if (!strncmp(key, "eff_", 4)) {
    const int eid = oplaits::FindParam(key + 4);
    if (eid < 0) return -1;
    return Finish(buf, size, FormatValue(eid, e.Effective(eid), buf, size));
  }
  // One read for the UI:
  // "<lanes with data>,<step or -1>,<recording lanes>,<loop steps>,<REC pad>"
  if (!strcmp(key, "motion_status"))
    return Finish(buf, size, snprintf(buf, size, "%u,%d,%u,%d,%d", e.motion_mask(), e.motion_step(),
                                      e.motion_recording(), e.steps(), e.write() ? 1 : 0));
  // The trigger pads, step 1 first: "1111111111111111".
  if (!strcmp(key, "gate_status")) {
    if (size <= 16) return -1;
    for (int i = 0; i < 16; ++i) buf[i] = (e.gates() >> i) & 1u ? '1' : '0';
    buf[16] = 0;
    return 16;
  }
  if (!strcmp(key, "write")) return Finish(buf, size, snprintf(buf, size, "%d", e.write() ? 1 : 0));
  // The engine the voice is actually rendering (after its own quantizer).
  if (!strcmp(key, "active_engine")) return Finish(buf, size, snprintf(buf, size, "%d", e.active_engine()));
  // The PITCH knob's note names for the scale in use, comma separated: the
  // UI shows "Eb2" rather than an index, and asks again after a key change.
  if (!strcmp(key, "pitch_names")) {
    const int r = static_cast<int>(e.GetParam(oplaits::P_ROOT));
    const int sc = static_cast<int>(e.GetParam(oplaits::P_SCALE));
    const int cnt = oplaits::PitchCount(r, sc);
    int n = 0;
    for (int i = 0; i < cnt && n >= 0 && n < size; ++i) {
      char nm[8];
      oplaits::NoteName(oplaits::PitchMidi(i, r, sc), nm, sizeof(nm));
      n += snprintf(buf + n, size - n, "%s%s", i ? "," : "", nm);
    }
    return Finish(buf, size, n);
  }
  if (!strcmp(key, "state")) return e.WriteState(buf, size);
  // PITCH's range is the number of notes the scale has in C1..C6, so the
  // knob has no dead travel whatever the scale (module.json carries 35, C
  // Phrygian's top index).
  if (!strcmp(key, "chain_params")) {
    const int top = oplaits::PitchCount(static_cast<int>(e.GetParam(oplaits::P_ROOT)),
                                        static_cast<int>(e.GetParam(oplaits::P_SCALE))) - 1;
    return Finish(buf, size, snprintf(buf, size, "%s%d%s", oplaits_chain_params_head, top,
                                      oplaits_chain_params_tail));
  }
  // Served, and EMPTY on purpose (see kHierarchy). An error here would read
  // as a failed read and hold the editor on "Loading..." (component_load_gate.mjs).
  if (!strcmp(key, "ui_hierarchy")) return 0;
  if (!strcmp(key, "grid_hierarchy")) return CopyText(buf, size, kHierarchy);
  return -1;
}

int get_error(void *ptr, char *buf, int size) {
  (void)ptr;
  if (buf && size > 0) buf[0] = 0;
  return 0;
}

void render_block(void *ptr, int16_t *out, int frames) {
  if (!out || frames <= 0) return;
  if (!ptr || frames > MOVE_FRAMES_PER_BLOCK) {
    memset(out, 0, static_cast<size_t>(frames) * 2 * sizeof(int16_t));
    return;
  }
  Instance *inst = static_cast<Instance *>(ptr);
  const bool has_transport = g_host && g_host->get_beat_position;
  const float bpm = (g_host && g_host->get_bpm) ? g_host->get_bpm() : 120.0f;
  inst->engine.Process(inst->mono, static_cast<size_t>(frames),
                       has_transport ? g_host->get_beat_position() : -1.0, bpm, has_transport);
  int peak = 0;
  for (int i = 0; i < frames; ++i) {
    const float s = SoftLimit(inst->mono[i]);
    const int16_t v = static_cast<int16_t>(lrintf(s * 32767.0f));
    out[2 * i] = v;
    out[2 * i + 1] = v;
    const int a = v < 0 ? -v : v;
    if (a > peak) peak = a;
  }
  if (peak <= kSilenceLsb && inst->engine.GetParam(oplaits::P_LEVEL) > 0.0f)
    for (int i = 0; i < frames * 2; ++i) out[i] = kKeepaliveLsb;
}

plugin_api_v2_t g_api = {};

}  // namespace

extern "C" __attribute__((visibility("default")))
plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
  if (host && host->sample_rate && host->sample_rate != MOVE_SAMPLE_RATE) return NULL;
  g_host = host;
  oplaits::RandomPhrase::LibraryInit();
  g_api.api_version = 2;
  g_api.create_instance = create_instance;
  g_api.destroy_instance = destroy_instance;
  g_api.on_midi = on_midi;
  g_api.set_param = set_param;
  g_api.get_param = get_param;
  g_api.get_error = get_error;
  g_api.render_block = render_block;
  return &g_api;
}
