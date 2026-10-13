// oPlaits - engine tests (native, ASan/UBSan). MIT.
//
// What is claimed in the README is asserted here:
//   - REFERENCE: for every one of the 24 engines, the wrapper's voice output
//     (at the voice's own rate, before the resampler) is BIT-IDENTICAL to an
//     official plaits::Voice driven directly -- Patch and Modulations filled
//     by hand the way the hardware's pots and jacks fill them, the TRIG gate
//     high while a note is held -- for OUT and AUX, through notes, legato
//     retriggers, releases, engine switches and parameter changes
//   - every engine's 16 KB RAM request fits (no allocation returns NULL)
//   - pitch is exact after the 47872.34 Hz -> 44.1 kHz resampler; the
//     resampler is flat in the audio band and rejects what would alias
//   - every engine renders finite, bounded audio and stops after its note
//   - AUX MIX selects OUT / AUX with equal power; VELO; TRIG OFF; CC 120 /
//     123; velocity-0 note-off; pitch bend; engine switching and knob fuzz
//     never crash or produce a non-finite sample (ASan/UBSan)
//   - the performance layer: the auto 1/16, the 16 trigger pads, PITCH in the
//     scale, touch motion, RANDOM, the state blob, no stuck notes
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <new>
#include <vector>

#include "plaits/dsp/voice.h"
#include "stmlib/utils/buffer_allocator.h"
#include "wrapper/oplaits.h"
#include "wrapper/scale.h"

using namespace oplaits;

static int g_failures = 0;
#define CHECK(cond, ...)                                   \
  do {                                                     \
    if (!(cond)) {                                         \
      ++g_failures;                                        \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);          \
      printf(__VA_ARGS__);                                 \
      printf("\n");                                        \
    }                                                      \
  } while (0)

static const float kFs = 44100.0f;
static const double kPlaitsFs = 72e6 / 1504.0;   // 47872.34 Hz
static OPlaits g_synth;   // static, like the plugin's pool

static std::vector<float> Render(OPlaits &e, float seconds) {
  std::vector<float> out(static_cast<size_t>(seconds * kFs));
  size_t n = 0;
  while (n < out.size()) {
    size_t m = out.size() - n;
    if (m > 128) m = 128;
    e.Process(&out[n], m, -1.0, 120.0f, true);
    n += m;
  }
  return out;
}

static std::vector<float> RenderPlaying(OPlaits &e, float seconds, double *beat, float bpm = 120.0f) {
  std::vector<float> out(static_cast<size_t>(seconds * kFs));
  size_t n = 0;
  while (n < out.size()) {
    size_t m = out.size() - n;
    if (m > 128) m = 128;
    e.Process(&out[n], m, *beat, bpm, true);
    *beat += m / kFs * bpm / 60.0;
    n += m;
  }
  return out;
}

static float Peak(const std::vector<float> &x, size_t from = 0, size_t to = 0) {
  if (to == 0 || to > x.size()) to = x.size();
  float p = 0.0f;
  for (size_t i = from; i < to; ++i) p = fmaxf(p, fabsf(x[i]));
  return p;
}

static float Rms(const std::vector<float> &x, size_t from, size_t to) {
  double s = 0.0;
  for (size_t i = from; i < to; ++i) s += static_cast<double>(x[i]) * x[i];
  return static_cast<float>(sqrt(s / static_cast<double>(to - from)));
}

// Frequency by rising zero crossings of the DC-removed signal, over [from, to).
static float Frequency(const std::vector<float> &x, size_t from, size_t to, float fs = kFs) {
  double mean = 0.0;
  for (size_t i = from; i < to; ++i) mean += x[i];
  mean /= static_cast<double>(to - from);
  int first = -1, last = -1, count = 0;
  double first_frac = 0.0, last_frac = 0.0;
  for (size_t i = from + 1; i < to; ++i) {
    const double a = x[i - 1] - mean, b = x[i] - mean;
    if (a < 0.0 && b >= 0.0) {
      const double frac = a / (a - b);   // linear interpolation of the crossing
      if (first < 0) { first = static_cast<int>(i); first_frac = frac; }
      last = static_cast<int>(i);
      last_frac = frac;
      ++count;
    }
  }
  if (count < 2) return 0.0f;
  const double span = (last - 1 + last_frac) - (first - 1 + first_frac);
  return static_cast<float>((count - 1) * fs / span);
}

static bool AllFinite(const std::vector<float> &x) {
  for (float v : x) if (!isfinite(v)) return false;
  return true;
}

static void Send(OPlaits &e, uint8_t a, uint8_t b, uint8_t c) {
  const uint8_t msg[3] = { a, b, c };
  e.Midi(msg, 3);
}
static void NoteOn(OPlaits &e, int note, int vel = 100) { Send(e, 0x90, note, vel); }
static void NoteOff(OPlaits &e, int note) { Send(e, 0x80, note, 0); }
static void Set(OPlaits &e, ParamId id, float v) { e.SetParam(id, v); }
static void Fresh(OPlaits &e) { e.Init(12345u); }

// ---------------------------------------------------------------------------
// REFERENCE: the official voice, driven by hand.
//
// A performance is a script of note-ons, note-offs, control changes and
// renders. It is played twice: once through the wrapper (MIDI and
// parameters, PlaitsEngine::RenderNative), once into an official
// plaits::Voice whose Patch and Modulations are filled here the way the
// hardware's pots and jacks fill them. stmlib's Random is one process-wide
// state, so each run starts from the same seed and they run one after the
// other, never interleaved.

static PlaitsEngine g_eng;
alignas(plaits::Voice) static unsigned char g_ref_mem[sizeof(plaits::Voice)];
alignas(16) static char g_ref_ram[16384];

static plaits::Voice &RefInit() {
  memset(g_ref_mem, 0, sizeof(g_ref_mem));
  memset(g_ref_ram, 0, sizeof(g_ref_ram));
  plaits::Voice *v = new (g_ref_mem) plaits::Voice();
  stmlib::BufferAllocator allocator(g_ref_ram, sizeof(g_ref_ram));
  v->Init(&allocator);
  return *v;
}

struct Step {
  enum Kind { ON, OFF, SET, RENDER } kind;
  int a;        // note / param id / block count
  float v;      // velocity / value
};

// The hardware's controls and jacks.
struct HwState {
  int engine;
  float harmonics, timbre, morph, decay, colour, fm, tim_amt, mor_amt;
  float pot_note;            // FREQUENCY pot, semitones
  float vcv;                 // V/OCT, semitones
  bool gate;                 // TRIG jack
  float level;               // LEVEL jack
  bool trig_patched, level_patched;
};

static void ApplyControl(HwState *h, int id, float v) {
  switch (id) {
    case P_ENGINE: h->engine = static_cast<int>(v); break;
    case P_HARMONICS: h->harmonics = v; break;
    case P_TIMBRE: h->timbre = v; break;
    case P_MORPH: h->morph = v; break;
    case P_DECAY: h->decay = v; break;
    case P_COLOUR: h->colour = v; break;
    case P_FM_AMT: h->fm = v; break;
    case P_TIMBRE_AMT: h->tim_amt = v; break;
    case P_MORPH_AMT: h->mor_amt = v; break;
    case P_COARSE: h->pot_note = v; break;
    case P_FINE: h->pot_note = v * 0.01f; break;   // scripts never set both
    case P_VELO: h->level_patched = v > 0.5f; break;
    case P_TRIG: h->trig_patched = v < 0.5f; break;
    default: break;
  }
}

static const uint32_t kSeed = 0x21;   // stmlib's own initial state

static void RunWrapper(const std::vector<Step> &script, std::vector<plaits::Voice::Frame> *out) {
  stmlib::Random::Seed(kSeed);
  g_eng.Init();
  out->clear();
  for (const Step &st : script) {
    if (st.kind == Step::ON) g_eng.NoteOn(static_cast<uint8_t>(st.a), static_cast<uint8_t>(st.v));
    else if (st.kind == Step::OFF) g_eng.NoteOff(static_cast<uint8_t>(st.a));
    else if (st.kind == Step::SET) g_eng.SetParam(st.a, st.v);
    else {
      const size_t at = out->size();
      out->resize(at + 12 * st.a);
      g_eng.RenderNative(&(*out)[at], st.a);
    }
  }
}

static void RunReference(const std::vector<Step> &script, std::vector<plaits::Voice::Frame> *out) {
  stmlib::Random::Seed(kSeed);
  plaits::Voice &ref = RefInit();
  HwState h = {};
  // The factory state: plaits/settings.cc, the pots at noon.
  h.engine = 8;
  h.harmonics = h.timbre = h.morph = 0.5f;
  h.decay = 0.5f;
  h.trig_patched = true;
  h.vcv = 60.0f;
  std::vector<int> held;   // the keys down, oldest first
  bool drop = false;       // a legato note-on: the gate falls for one block
  out->clear();
  for (const Step &st : script) {
    if (st.kind == Step::ON) {
      // Any key already down -- the same one included -- means the gate is
      // high: a new trigger needs it to fall first.
      if (!held.empty()) drop = true;
      for (size_t i = 0; i < held.size(); ++i) if (held[i] == st.a) { held.erase(held.begin() + i); break; }
      held.push_back(st.a);
      h.vcv = static_cast<float>(st.a);
      h.level = st.v / 127.0f;
      h.gate = true;
    } else if (st.kind == Step::OFF) {
      for (size_t i = 0; i < held.size(); ++i) if (held[i] == st.a) { held.erase(held.begin() + i); break; }
      if (!held.empty()) h.vcv = static_cast<float>(held.back());
      else h.gate = false;
    } else if (st.kind == Step::SET) {
      ApplyControl(&h, st.a, st.v);
    } else {
      for (int b = 0; b < st.a; ++b) {
        plaits::Patch p;
        plaits::Modulations m;
        memset(&p, 0, sizeof(p));
        memset(&m, 0, sizeof(m));
        p.engine = h.engine;
        p.note = h.pot_note;
        p.harmonics = h.harmonics;
        p.timbre = h.timbre;
        p.morph = h.morph;
        p.frequency_modulation_amount = h.fm;
        p.timbre_modulation_amount = h.tim_amt;
        p.morph_modulation_amount = h.mor_amt;
        p.decay = h.decay;
        p.lpg_colour = h.colour;
        m.note = h.vcv;
        m.trigger = (h.gate && !drop) ? 1.0f : 0.0f;
        m.level = h.gate ? h.level : 0.0f;
        m.trigger_patched = h.trig_patched;
        m.level_patched = h.level_patched;
        drop = false;
        plaits::Voice::Frame f[12];
        ref.Render(p, m, f, 12);
        out->insert(out->end(), f, f + 12);
      }
    }
  }
}

static std::vector<Step> Script(int engine, int config) {
  std::vector<Step> s;
  auto set = [&](int id, float v) { s.push_back({Step::SET, id, v}); };
  auto on = [&](int n, int vel) { s.push_back({Step::ON, n, static_cast<float>(vel)}); };
  auto off = [&](int n) { s.push_back({Step::OFF, n, 0.0f}); };
  auto render = [&](int blocks) { s.push_back({Step::RENDER, blocks, 0.0f}); };
  set(P_ENGINE, static_cast<float>(engine));
  set(P_HARMONICS, 0.13f + 0.03f * engine);
  set(P_TIMBRE, 0.71f);
  set(P_MORPH, 0.27f);
  set(P_DECAY, 0.62f);
  set(P_COLOUR, 0.4f);
  if (config == 1) {                  // VELO on, attenuverters set
    set(P_VELO, 1.0f);
    set(P_FM_AMT, 0.6f);
    set(P_TIMBRE_AMT, -0.5f);
    set(P_MORPH_AMT, 0.8f);
  } else if (config == 2) {           // TRIG unpatched, LEVEL patched, transposed
    set(P_TRIG, 1.0f);
    set(P_VELO, 1.0f);
    set(P_COARSE, -12.0f);
  }
  render(3);                          // before any note
  on(48, 100); render(200);
  on(55, 64); render(150);            // legato: a retrigger
  on(60, 127); render(100);
  off(60); render(100);               // back to 55, no trigger
  off(55); off(48); render(400);      // release
  on(36, 90);
  for (int k = 0; k < 20; ++k) {      // a sweep under the note
    set(P_TIMBRE, k / 19.0f);
    set(P_MORPH, 1.0f - k / 19.0f);
    set(P_HARMONICS, fmodf(0.37f * k, 1.0f));
    render(10);
  }
  set(P_ENGINE, static_cast<float>((engine + 7) % kNumEngines));   // switch mid-note
  render(100);
  set(P_ENGINE, static_cast<float>(engine));
  on(36, 90); render(200);            // and back, retriggered
  return s;
}

static void TestReferenceBitExact() {
  std::vector<plaits::Voice::Frame> a, b;
  int total_bad = 0;
  for (int engine = 0; engine < kNumEngines; ++engine) {
    for (int config = 0; config < 3; ++config) {
      const std::vector<Step> script = Script(engine, config);
      RunWrapper(script, &a);
      RunReference(script, &b);
      CHECK(a.size() == b.size() && !a.empty(), "script lengths");
      int bad = 0;
      long sound = 0;
      for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (a[i].out != b[i].out || a[i].aux != b[i].aux) {
          if (!bad) printf("    engine %d config %d: first difference at sample %zu: %d/%d vs %d/%d\n",
                           engine, config, i, a[i].out, a[i].aux, b[i].out, b[i].aux);
          ++bad;
        }
        if (abs(a[i].out) > 64) ++sound;
      }
      CHECK(bad == 0, "engine %d config %d: %d samples differ from the official voice", engine, config, bad);
      CHECK(sound > 1000, "engine %d config %d: the comparison saw no sound", engine, config);
      total_bad += bad;
    }
  }
  if (total_bad == 0)
    printf("  reference: 24 engines x 3 configs, OUT and AUX bit-identical to the official voice\n");
}

// Each engine's Init() takes its buffers from the shared 16 KB; a request
// that did not fit would come back NULL and crash on first use.
template <typename E>
static size_t Needs(E *e) {
  static char big[1 << 20];
  stmlib::BufferAllocator a(big, sizeof(big));
  e->Init(&a);
  return sizeof(big) - a.free();
}

static void TestEngineRam() {
  static plaits::VirtualAnalogVCFEngine e0; static plaits::PhaseDistortionEngine e1;
  static plaits::SixOpEngine e2; static plaits::WaveTerrainEngine e5;
  static plaits::StringMachineEngine e6; static plaits::ChiptuneEngine e7;
  static plaits::VirtualAnalogEngine e8; static plaits::WaveshapingEngine e9;
  static plaits::FMEngine e10; static plaits::GrainEngine e11; static plaits::AdditiveEngine e12;
  static plaits::WavetableEngine e13; static plaits::ChordEngine e14; static plaits::SpeechEngine e15;
  static plaits::SwarmEngine e16; static plaits::NoiseEngine e17; static plaits::ParticleEngine e18;
  static plaits::StringEngine e19; static plaits::ModalEngine e20; static plaits::BassDrumEngine e21;
  static plaits::SnareDrumEngine e22; static plaits::HiHatEngine e23;
  const size_t need[] = {Needs(&e0), Needs(&e1), Needs(&e2), Needs(&e5), Needs(&e6), Needs(&e7),
                         Needs(&e8), Needs(&e9), Needs(&e10), Needs(&e11), Needs(&e12), Needs(&e13),
                         Needs(&e14), Needs(&e15), Needs(&e16), Needs(&e17), Needs(&e18), Needs(&e19),
                         Needs(&e20), Needs(&e21), Needs(&e22), Needs(&e23)};
  size_t most = 0;
  for (size_t n : need) {
    CHECK(n <= kSharedRam, "an engine needs %zu bytes of the %zu shared", n, kSharedRam);
    if (n > most) most = n;
  }
  printf("  RAM: largest engine request %zu of %zu bytes; sizeof(Voice) %zu, sizeof(OPlaits) %zu\n",
         most, kSharedRam, sizeof(plaits::Voice), sizeof(OPlaits));
}

// ---------------------------------------------------------------------------

static void TestPitch() {
  // VA (engine 8): HARMONICS 0.5 is the unison point of its detune (0 puts
  // the second oscillator two octaves down, as on the hardware).
  const int notes[] = { 45, 57, 69, 81 };
  for (int note : notes) {
    Fresh(g_synth);
    Set(g_synth, P_ENGINE, 8);
    Set(g_synth, P_HARMONICS, 0.5f);
    Set(g_synth, P_TIMBRE, 0.0f);
    Set(g_synth, P_MORPH, 0.0f);
    Set(g_synth, P_TRIG, 1);              // free running: steady tone
    Set(g_synth, P_VELO, 1);              // LEVEL = velocity: open while held
    NoteOn(g_synth, note);
    std::vector<float> x = Render(g_synth, 1.5f);
    const float want = 440.0f * powf(2.0f, (note - 69) / 12.0f);
    const float got = Frequency(x, 22050, x.size());
    CHECK(fabsf(got / want - 1.0f) < 0.0006f, "note %d: %.3f Hz, want %.3f (%.2f cents)", note, got,
          want, 1200.0f * log2f(got / want));
    if (note == 69) printf("  pitch: A4 = %.3f Hz (%.2f cents)\n", got, 1200.0f * log2f(got / want));
  }
  // COARSE / FINE / bend.
  Fresh(g_synth);
  Set(g_synth, P_ENGINE, 8);
  Set(g_synth, P_HARMONICS, 0.5f); Set(g_synth, P_TIMBRE, 0.0f); Set(g_synth, P_MORPH, 0.0f);
  Set(g_synth, P_TRIG, 1); Set(g_synth, P_VELO, 1);
  Set(g_synth, P_COARSE, 12);
  Set(g_synth, P_FINE, 50);
  NoteOn(g_synth, 57);
  std::vector<float> x = Render(g_synth, 1.5f);
  float got = Frequency(x, 22050, x.size());
  float want = 440.0f * powf(2.0f, 0.5f / 12.0f);
  CHECK(fabsf(got / want - 1.0f) < 0.0006f, "coarse+fine: %.3f Hz, want %.3f", got, want);
  Send(g_synth, 0xE0, 0x7F, 0x7F);         // bend up 2 semitones
  x = Render(g_synth, 1.0f);
  got = Frequency(x, 11025, x.size());
  want = 440.0f * powf(2.0f, 2.5f / 12.0f);
  CHECK(fabsf(got / want - 1.0f) < 0.001f, "bend: %.3f Hz, want %.3f", got, want);
}

// The resampler alone: a sine at the voice's rate in, measured out.
static void TestResampler() {
  static Resampler rs;
  const float freqs[] = { 1000.0f, 10000.0f, 18000.0f, 20000.0f, 23700.0f };
  for (float f : freqs) {
    rs.Reset();
    std::vector<float> out;
    double ph = 0.0;
    while (out.size() < 44100) {
      while (rs.NeedsInput()) {
        rs.Push(static_cast<float>(sin(ph)));
        ph += 2.0 * M_PI * f / kPlaitsFs;
      }
      out.push_back(rs.Next());
    }
    const float gain = Rms(out, 4410, out.size()) * sqrtf(2.0f);
    const float db = 20.0f * log10f(gain + 1e-12f);
    if (f <= 18000.0f) {
      CHECK(fabsf(db) < 0.05f, "resampler %.0f Hz: %.3f dB, want flat", f, db);
      const float got = Frequency(out, 4410, out.size());
      CHECK(fabsf(got / f - 1.0f) < 1e-4f, "resampler moved %.0f Hz to %.2f", f, got);
    } else if (f <= 20000.0f) {
      CHECK(db > -1.0f, "resampler %.0f Hz: %.2f dB", f, db);
    } else {
      CHECK(db < -60.0f, "resampler %.0f Hz (would alias): only %.1f dB down", f, db);
    }
    printf("  resampler: %5.0f Hz %7.2f dB\n", f, db);
  }
}

static const char *const kNames[kNumEngines] = {
    "VAVCF", "PHDIS", "6OP1", "6OP2", "6OP3", "WTERR", "STRMC", "CHIP",
    "VA", "WSHAP", "FM", "GRAIN", "ADDIT", "WTBL", "CHORD", "SPEAK",
    "SWARM", "NOISE", "PARTI", "STRNG", "MODAL", "KICK", "SNARE", "HIHAT"};

static void TestEngines() {
  for (int m = 0; m < kNumEngines; ++m) {
    Fresh(g_synth);
    Set(g_synth, P_ENGINE, static_cast<float>(m));
    Set(g_synth, P_LEVEL, 1.0f);
    // CHIP with TRIG patched bypasses the LPG and takes its own envelope
    // shape from the TIMBRE attenuverter: at the centre it never decays
    // (official 1.2 behaviour), so it is given a decaying shape here.
    if (m == 7) Set(g_synth, P_TIMBRE_AMT, 0.5f);
    NoteOn(g_synth, 48);
    std::vector<float> x = Render(g_synth, 0.5f);
    NoteOff(g_synth, 48);
    std::vector<float> tail = Render(g_synth, 4.0f);
    CHECK(AllFinite(x) && AllFinite(tail), "engine %d: non-finite output", m);
    const float p = Peak(x);
    CHECK(p > 0.02f && p <= 1.0f, "engine %d (%s): peak %.3f", m, kNames[m], p);
    const float rest = Peak(tail, tail.size() - 4410);
    // The 6-op engines release by the DX7 patch's own envelopes (bank 1's
    // first patch rings long): fading, not silent, after 4 s.
    const float allowed = (m >= 2 && m <= 4) ? 0.1f * p : 2e-3f;
    CHECK(rest < allowed, "engine %d (%s) still sounding 3.9 s after its note: %.4f", m, kNames[m], rest);
    CHECK(g_synth.active_engine() == m, "engine %d: voice renders %d", m, g_synth.active_engine());
    printf("  engine %2d %-5s peak %.3f  rms %.3f\n", m, kNames[m], p, Rms(x, 0, x.size()));
  }
}

static void TestAuxMix() {
  // OUT and AUX of the VA engine differ; AUX MIX 1 is AUX alone, 0 OUT
  // alone, 0.5 both at -3 dB each.
  float rms[3];
  const float mixes[3] = { 0.0f, 1.0f, 0.5f };
  std::vector<float> xs[3];
  for (int i = 0; i < 3; ++i) {
    Fresh(g_synth);
    Set(g_synth, P_ENGINE, 8);
    Set(g_synth, P_TRIG, 1); Set(g_synth, P_VELO, 1);
    Set(g_synth, P_AUX_MIX, mixes[i]);
    NoteOn(g_synth, 57, 127);
    xs[i] = Render(g_synth, 0.5f);
    rms[i] = Rms(xs[i], 4410, xs[i].size());
  }
  double diff = 0.0;
  for (size_t n = 4410; n < xs[0].size(); ++n) diff += fabs(xs[0][n] - xs[1][n]);
  CHECK(diff > 10.0, "AUX MIX: OUT and AUX are the same signal");
  double err = 0.0;
  const float c = cosf(0.25f * static_cast<float>(M_PI)), s = sinf(0.25f * static_cast<float>(M_PI));
  for (size_t n = 4410; n < xs[0].size(); ++n) err = fmax(err, fabs(xs[2][n] - (c * xs[0][n] + s * xs[1][n])));
  CHECK(err < 1e-4, "AUX MIX 0.5 is not the equal-power sum (%.6f)", err);
  printf("  aux mix: OUT rms %.3f, AUX rms %.3f, 50%% rms %.3f\n", rms[0], rms[1], rms[2]);
}

static void TestVelocity() {
  // VELO off: velocity changes nothing (accent 0.8, as with LEVEL unpatched).
  float r[2];
  for (int i = 0; i < 2; ++i) {
    Fresh(g_synth);
    Set(g_synth, P_ENGINE, 21);
    NoteOn(g_synth, 36, i ? 30 : 127);
    std::vector<float> x = Render(g_synth, 0.3f);
    r[i] = Rms(x, 0, x.size());
  }
  CHECK(r[0] == r[1], "VELO OFF: velocity changed the kick (%.4f vs %.4f)", r[0], r[1]);
  // VELO on: softer velocity, softer kick (accent) and quieter VA (LPG).
  for (int engine : {21, 8}) {
    for (int i = 0; i < 2; ++i) {
      Fresh(g_synth);
      Set(g_synth, P_ENGINE, static_cast<float>(engine));
      Set(g_synth, P_VELO, 1);
      NoteOn(g_synth, 36, i ? 30 : 127);
      std::vector<float> x = Render(g_synth, 0.3f);
      r[i] = Rms(x, 0, x.size());
    }
    CHECK(r[1] < 0.8f * r[0], "VELO ON engine %d: velocity 30 %.4f vs 127 %.4f", engine, r[1], r[0]);
  }
}

static void TestRetrigger() {
  // Two note-ons on the kick while the key stays down: two hits.
  Fresh(g_synth);
  Set(g_synth, P_ENGINE, 21);
  Set(g_synth, P_DECAY, 0.2f);
  Set(g_synth, P_MORPH, 0.2f);       // short kick
  NoteOn(g_synth, 36);
  std::vector<float> a = Render(g_synth, 0.5f);
  NoteOn(g_synth, 36);               // same note again, still held
  std::vector<float> b = Render(g_synth, 0.2f);
  CHECK(Peak(a, 18000) < 0.05f, "kick did not decay: %.3f", Peak(a, 18000));
  CHECK(Peak(b) > 0.8f * Peak(a), "a repeated note-on did not retrigger: %.3f vs first %.3f", Peak(b), Peak(a));
}

static void TestTrigOffAndPanic() {
  // TRIG OFF + VELO OFF is the hardware's drone: it plays without a gate --
  // but only after a note has been played (silent at load), and CC 120 shuts it.
  Fresh(g_synth);
  Set(g_synth, P_ENGINE, 8);
  Set(g_synth, P_TRIG, 1);
  std::vector<float> x = Render(g_synth, 0.2f);
  CHECK(Peak(x) == 0.0f, "drone sounded before any note");
  NoteOn(g_synth, 57);
  NoteOff(g_synth, 57);
  x = Render(g_synth, 0.5f);
  CHECK(Peak(x, 11025) > 0.05f, "drone mode is silent");
  Send(g_synth, 0xB0, 120, 0);
  x = Render(g_synth, 0.2f);
  CHECK(Peak(x, 2205) < 1e-3f, "CC 120 did not silence the drone: %.4f", Peak(x, 2205));
  // CC 123 releases held keys; velocity-0 note-on is a note-off.
  Fresh(g_synth);
  Set(g_synth, P_ENGINE, 8);
  Set(g_synth, P_VELO, 1);
  NoteOn(g_synth, 57);
  Render(g_synth, 0.1f);
  Send(g_synth, 0xB0, 123, 0);
  CHECK(!g_synth.key_held(), "CC 123 left a key held");
  NoteOn(g_synth, 57);
  Send(g_synth, 0x90, 57, 0);
  CHECK(!g_synth.key_held(), "velocity-0 note-on did not release");
  x = Render(g_synth, 3.0f);
  CHECK(Peak(x, x.size() - 4410) < 2e-3f, "released VA still sounding");
}

static void TestSwitchAndFuzz() {
  // Engine switches every few blocks while notes play, and every control at
  // random values, including the extremes, for a minute of audio.
  Fresh(g_synth);
  unsigned r = 99;
  float buf[128];
  double beat = 0.0;
  long blocks = 0;
  for (int i = 0; i < 6000; ++i) {
    r = r * 1664525u + 1013904223u;
    const int what = (r >> 24) % 12;
    const float u = ((r >> 8) & 0xFFFF) / 65535.0f;
    switch (what) {
      case 0: Set(g_synth, P_ENGINE, static_cast<float>((r >> 4) % kNumEngines)); break;
      case 1: Set(g_synth, P_HARMONICS, u); break;
      case 2: Set(g_synth, P_TIMBRE, u); break;
      case 3: Set(g_synth, P_MORPH, u); break;
      case 4: Set(g_synth, P_DECAY, u); Set(g_synth, P_COLOUR, 1.0f - u); break;
      case 5: Set(g_synth, P_FM_AMT, 2 * u - 1); Set(g_synth, P_TIMBRE_AMT, 1 - 2 * u); break;
      case 6: Set(g_synth, P_MORPH_AMT, 2 * u - 1); Set(g_synth, P_AUX_MIX, u); break;
      case 7: NoteOn(g_synth, static_cast<int>((r >> 3) % 128), 1 + static_cast<int>((r >> 11) % 127)); break;
      case 8: NoteOff(g_synth, static_cast<int>((r >> 3) % 128)); break;
      case 9: Set(g_synth, P_VELO, static_cast<float>((r >> 5) & 1)); Set(g_synth, P_TRIG, static_cast<float>((r >> 6) & 1)); break;
      case 10: Set(g_synth, P_COARSE, static_cast<float>(static_cast<int>((r >> 4) % 49) - 24)); Send(g_synth, 0xE0, (r >> 7) & 0x7F, (r >> 14) & 0x7F); break;
      default: beat = ((r >> 9) & 1) ? -1.0 : beat < 0 ? 0.0 : beat; break;
    }
    const int n = 1 + static_cast<int>((r >> 16) % 6);
    for (int b = 0; b < n; ++b) {
      g_synth.Process(buf, 128, beat, 120.0f, true);
      if (beat >= 0.0) beat += 128.0 / kFs * 2.0;
      for (float v : buf) {
        if (!isfinite(v) || fabsf(v) > 1.0f) { CHECK(false, "fuzz: bad sample %f", v); goto done; }
      }
      ++blocks;
    }
  }
done:
  printf("  fuzz: %ld blocks (%.0f s) of random switching and knobs\n", blocks, blocks * 128 / kFs);
}

// ---------------------------------------------------------------------------

static char g_state_a[4096], g_state_b[4096];
static OPlaits g_other;

static void TestStateRoundTrip() {
  Fresh(g_synth);
  Set(g_synth, P_ENGINE, 20);
  Set(g_synth, P_HARMONICS, 0.33f);
  Set(g_synth, P_TIMBRE, 0.45f);
  Set(g_synth, P_MORPH, 0.62f);
  Set(g_synth, P_DECAY, 0.8f);
  Set(g_synth, P_COLOUR, 0.25f);
  Set(g_synth, P_FM_AMT, -0.4f);
  Set(g_synth, P_AUX_MIX, 0.75f);
  Set(g_synth, P_LEVEL, 0.9f);
  Set(g_synth, P_VELO, 1);
  Set(g_synth, P_COARSE, -7);
  const int n = g_synth.WriteState(g_state_a, sizeof(g_state_a));
  CHECK(n > 0, "WriteState");
  printf("  state: %s\n", g_state_a);
  CHECK(strstr(g_state_a, "\"engine\":20,") != NULL, "MODAL not stored as engine 20");
  CHECK(strstr(g_state_a, "\"aux_mix\":0.7500,") != NULL, "AUX MIX not stored");
  g_other.Init(777u);
  CHECK(g_other.ReadState(g_state_a), "ReadState");
  g_other.WriteState(g_state_b, sizeof(g_state_b));
  CHECK(!strcmp(g_state_a, g_state_b), "round trip differs:\n  %s\n  %s", g_state_a, g_state_b);
  // ...and it SOUNDS the same: the engine and every control came back.
  NoteOn(g_synth, 50); NoteOn(g_other, 50);
  std::vector<float> a = Render(g_synth, 0.3f), b = Render(g_other, 0.3f);
  CHECK(a == b, "restored state renders differently");
  CHECK(g_other.active_engine() == 20, "restored engine %d", g_other.active_engine());

  // Robustness: whitespace, missing keys keep their value, junk rejected.
  g_other.Init(777u);
  CHECK(g_other.ReadState(" { \"v\" : 1 , \"engine\" : 3 } "), "pretty JSON");
  CHECK(g_other.GetParam(P_ENGINE) == 3.0f && g_other.GetParam(P_DECAY) == 0.5f, "partial state");
  CHECK(!g_other.ReadState("{\"v\":2,\"engine\":5}"), "future version accepted");
  CHECK(g_other.GetParam(P_ENGINE) == 3.0f, "rejected state still applied");
  CHECK(!g_other.ReadState("{\"engine\":5"), "truncated state accepted");
  g_other.ReadState("{\"engine\":99,\"fm_amt\":-4,\"timbre\":1e9}");
  CHECK(g_other.GetParam(P_ENGINE) == 23.0f && g_other.GetParam(P_FM_AMT) == -1.0f &&
        g_other.GetParam(P_TIMBRE) == 1.0f, "out-of-range values not clamped");
  g_other.Init(777u);
  g_other.ReadState("{\"x\":{\"engine\":1}}");
  CHECK(g_other.GetParam(P_ENGINE) == 8.0f, "nested key leaked into ENGINE");
}

// ---------------------------------------------------------------------------
// The auto 1/16, the pads and the motion.

static bool InPhrygian(int midi) {
  const int pc = midi % 12;
  return pc == 0 || pc == 1 || pc == 3 || pc == 5 || pc == 7 || pc == 8 || pc == 10;
}

static void TestAutoSixteenths() {
  Fresh(g_synth);
  std::vector<float> x = Render(g_synth, 0.5f);
  CHECK(Peak(x) == 0.0f, "sounded with the transport stopped");
  double beat = 0.0;
  x = RenderPlaying(g_synth, 1.0f, &beat);
  CHECK(Peak(x) > 0.05f, "silent with the transport running");
  // PITCH is the note: G2 = 98 Hz (MIDI 43).
  Fresh(g_synth);
  Set(g_synth, P_HARMONICS, 0.5f); Set(g_synth, P_TIMBRE, 0.0f); Set(g_synth, P_MORPH, 0.0f);
  Set(g_synth, P_DECAY, 0.8f);
  Set(g_synth, P_PITCH, static_cast<float>(PitchIndexOfMidi(43.0f, 0, 0)));
  beat = 0.0;
  x = RenderPlaying(g_synth, 2.0f, &beat);
  const float got = Frequency(x, 8 * 5513 + 600, 9 * 5512 - 100);
  CHECK(fabsf(got / 98.0f - 1.0f) < 0.01f, "PITCH G2: %.2f Hz, want 98", got);
  Set(g_synth, P_DECAY, 0.5f);
  for (int i = 0; i < 16; ++i) g_synth.ToggleGate(i);
  CHECK(g_synth.gates() == 0, "gates %04x", g_synth.gates());
  x = RenderPlaying(g_synth, 4.0f, &beat);
  CHECK(Peak(x, x.size() - 4410) < 2e-3f, "all trigger pads off still sounds: %.5f", Peak(x, x.size() - 4410));
  g_synth.ToggleGate(0);
  // 1.2 s: the render starts ON a step-1 boundary, which the previous render
  // may already have fired (with every pad off); the next step 1 is 1 s on.
  x = RenderPlaying(g_synth, 1.2f, &beat);
  CHECK(Peak(x) > 0.05f, "a single trigger pad does not play: %.4f", Peak(x));
  x = Render(g_synth, 4.0f);
  CHECK(Peak(x, x.size() - 4410) < 2e-3f, "Stop left a note sounding: %.5f", Peak(x, x.size() - 4410));
  CHECK(!g_synth.key_held(), "Stop left a key held");
}

static void TestHitsLandOnTheSixteenth() {
  // The kick, short: each hit starts on its 1/16 (5512.5 samples at 120
  // BPM), within the voice's own 1.25 ms trigger delay + resampler + block.
  Fresh(g_synth);
  Set(g_synth, P_ENGINE, 21);
  Set(g_synth, P_DECAY, 0.1f);
  Set(g_synth, P_MORPH, 0.1f);
  Set(g_synth, P_LEVEL, 1.0f);
  Render(g_synth, 0.1f);
  double beat = 0.0;
  std::vector<float> x = RenderPlaying(g_synth, 1.0f, &beat);
  int bad = 0;
  for (int k = 1; k < 7; ++k) {
    const size_t at = static_cast<size_t>(k * 5512.5);
    const float before = Peak(x, at - 300, at - 20);
    const float after = Peak(x, at, at + 160);
    if (!(before < 0.05f && after > 0.1f)) {
      ++bad;
      printf("    hit %d: before %.3f after %.3f\n", k, before, after);
    }
  }
  CHECK(bad == 0, "%d of 6 hits not on their 1/16", bad);
}

static void TestTouchMotion() {
  Fresh(g_synth);
  const unsigned kT = 1u << Motion::LaneOf(P_TIMBRE), kC = 1u << Motion::LaneOf(P_COLOUR);
  CHECK(Motion::LaneOf(P_ENGINE) < 0, "ENGINE must never be recorded");
  CHECK(Motion::LaneOf(P_LEVEL) < 0, "VOLUME must never be recorded");
  double beat = 0.0;
  RenderPlaying(g_synth, 0.1f, &beat);
  g_synth.Touch(P_TIMBRE, true);
  RenderPlaying(g_synth, 1.1f, &beat);
  g_synth.Touch(P_TIMBRE, false);
  CHECK(g_synth.motion_mask() == 0, "recorded with REC off");
  g_synth.SetWrite(true);
  Set(g_synth, P_TIMBRE, 0.8f);
  for (int i = 0; i < 9; ++i) {
    g_synth.Touch(P_TIMBRE, true);
    RenderPlaying(g_synth, 0.13f, &beat);
    CHECK(i == 0 || (g_synth.motion_recording() & kT), "not recording under the finger");
  }
  g_synth.Touch(P_TIMBRE, false);
  CHECK(g_synth.motion_mask() == kT, "timbre lane %x", g_synth.motion_mask());
  Set(g_synth, P_TIMBRE, 0.2f);
  RenderPlaying(g_synth, 0.2f, &beat);
  CHECK(fabsf(g_synth.Effective(P_TIMBRE) - 0.8f) < 0.01f, "playback %.3f", g_synth.Effective(P_TIMBRE));
  CHECK(g_synth.GetParam(P_TIMBRE) == 0.2f, "playback wrote the knob");
  Render(g_synth, 0.05f);
  CHECK(g_synth.Effective(P_TIMBRE) == 0.2f, "stopped still plays the lane");
  beat = 0.0;
  g_synth.Touch(P_COLOUR, true);
  RenderPlaying(g_synth, 0.7f, &beat);
  CHECK(!(g_synth.motion_recording() & kC), "a touch outlived its heartbeat");
  g_synth.ClearMotion(P_TIMBRE);
  CHECK(!(g_synth.motion_mask() & kT), "clear timbre");
  g_synth.ClearMotion(-1);
  CHECK(g_synth.motion_mask() == 0, "clear all");
}

static void TestRandomAndScale() {
  Fresh(g_synth);
  for (int n = 0; n < 50; ++n) {
    g_synth.Randomize();
    CHECK(g_synth.motion_mask() & (1u << Motion::LaneOf(P_PITCH)), "RANDOM left the PITCH lane empty");
    double beat = 0.0;
    for (int st = 0; st < 8; ++st) {
      RenderPlaying(g_synth, 0.125f, &beat);
      const int midi = PitchMidi(static_cast<int>(g_synth.Effective(P_PITCH) + 0.5f), 0, 0);
      CHECK(InPhrygian(midi), "RANDOM note %d not in C Phrygian", midi);
    }
    Render(g_synth, 0.02f);
  }
  const int before = PitchMidi(static_cast<int>(g_synth.GetParam(P_PITCH)), 0, 0);
  Set(g_synth, P_SCALE, 3);
  const int after = PitchMidi(static_cast<int>(g_synth.GetParam(P_PITCH)), 0, 3);
  CHECK(abs(after - before) <= 1, "knob jumped %d -> %d on a scale change", before, after);
  Set(g_synth, P_SCALE, 9);
  Set(g_synth, P_PITCH, 60.0f);
  CHECK(g_synth.GetParam(P_PITCH) == static_cast<float>(PitchCount(0, 9) - 1), "PITCH past the scale");
}

static void TestSixteenStepLoop() {
  Fresh(g_synth);
  Set(g_synth, P_STEPS, 1);
  CHECK(g_synth.steps() == 16, "steps");
  double beat = 0.0;
  int maxstep = -1;
  for (int i = 0; i < 40; ++i) {
    RenderPlaying(g_synth, 0.06f, &beat);
    if (g_synth.motion_step() > maxstep) maxstep = g_synth.motion_step();
  }
  CHECK(maxstep == 15, "16-step loop reached step %d", maxstep + 1);
}

static char g_state_c[16384], g_state_d[16384];
static OPlaits g_third;

static void TestMotionState() {
  Fresh(g_synth);
  g_synth.Randomize();
  g_synth.ToggleGate(3);
  Set(g_synth, P_STEPS, 1);
  // A recorded MORPH lane too.
  g_synth.SetWrite(true);
  double beat = 0.0;
  RenderPlaying(g_synth, 0.05f, &beat);
  Set(g_synth, P_MORPH, 0.9f);
  g_synth.Touch(P_MORPH, true);
  RenderPlaying(g_synth, 0.3f, &beat);
  g_synth.Touch(P_MORPH, false);
  g_synth.WriteState(g_state_c, sizeof(g_state_c));
  CHECK(strstr(g_state_c, "pitch:") != NULL && strstr(g_state_c, "morph:") != NULL, "lanes not saved");
  CHECK(strstr(g_state_c, "\"g\":65527}") != NULL, "gates not saved");
  CHECK(strlen(g_state_c) < 16384, "state too large for MAX_SYNTH_STATE_LEN");
  g_third.Init(1u);
  CHECK(g_third.ReadState(g_state_c), "ReadState");
  g_third.WriteState(g_state_d, sizeof(g_state_d));
  CHECK(!strcmp(g_state_c, g_state_d), "motion round trip differs");
  g_third.Init(1u);
  CHECK(g_third.ReadState("{\"v\":1,\"decay\":0.9,\"m\":\"pitch:!!\",\"g\":1}"), "partial");
  CHECK(fabsf(g_third.GetParam(P_DECAY) - 0.9f) < 1e-6f && g_third.motion_mask() == 0, "corrupt lane applied");
}

static void TestNoStuckNotesFuzz() {
  Fresh(g_synth);
  double beat = 0.0;
  unsigned r = 7;
  float buf[128];
  for (int i = 0; i < 3000; ++i) {
    r = r * 1664525u + 1013904223u;
    switch ((r >> 24) % 9) {
      case 0: g_synth.Randomize(); break;
      case 1: g_synth.ToggleGate(static_cast<int>((r >> 8) % 16)); break;
      case 2: Set(g_synth, P_STEPS, static_cast<float>((r >> 9) & 1)); break;
      case 3: Set(g_synth, P_PITCH, static_cast<float>((r >> 10) % 36)); break;
      case 4: g_synth.SetWrite(((r >> 11) & 1) != 0); g_synth.Touch(P_PITCH, ((r >> 12) & 1) != 0); break;
      case 5: beat = (r >> 12) % 64 / 4.0; break;
      case 6: for (int b = 0; b < 3; ++b) g_synth.Process(buf, 128, -1.0, 120.0f, true); break;
      case 7: Set(g_synth, P_SCALE, static_cast<float>((r >> 13) % 12)); break;
      default: Set(g_synth, P_ENGINE, static_cast<float>((r >> 5) % kNumEngines)); break;
    }
    const int n = 1 + static_cast<int>((r >> 16) % 20);
    for (int b = 0; b < n; ++b) {
      g_synth.Process(buf, 128, beat, 120.0f, true);
      beat += 128.0 / kFs * 2.0;
      for (float v : buf) CHECK(isfinite(v), "fuzz: non-finite output");
    }
  }
  Set(g_synth, P_ENGINE, 8);
  std::vector<float> x = Render(g_synth, 4.0f);
  CHECK(!g_synth.key_held(), "fuzz: a key left held after Stop");
  CHECK(Peak(x, x.size() - 4410) < 2e-3f, "fuzz: sounding after Stop %.5f", Peak(x, x.size() - 4410));
}

// ---------------------------------------------------------------------------

static double BlockMicros(OPlaits &e, int blocks) {
  float buf[128];
  timespec a, b;
  clock_gettime(CLOCK_MONOTONIC, &a);
  for (int i = 0; i < blocks; ++i) e.Process(buf, 128, -1.0, 120.0f, true);
  clock_gettime(CLOCK_MONOTONIC, &b);
  return ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 1e3 / blocks;
}

static void ReportCost() {
  for (int m = 0; m < kNumEngines; m += 1) {
    Fresh(g_synth);
    Set(g_synth, P_ENGINE, static_cast<float>(m));
    Set(g_synth, P_TRIG, 1); Set(g_synth, P_VELO, 1);
    NoteOn(g_synth, 48);
    if (m % 6 == 0)
      printf("  cost (this machine, sanitized): engine %2d %.1f us/block\n", m, BlockMicros(g_synth, 500));
  }
}

int main() {
  setvbuf(stdout, NULL, _IONBF, 0);   // nothing lost if a sanitizer aborts
  TestReferenceBitExact();
  TestEngineRam();
  TestPitch();
  TestResampler();
  TestEngines();
  TestAuxMix();
  TestVelocity();
  TestRetrigger();
  TestTrigOffAndPanic();
  TestSwitchAndFuzz();
  TestStateRoundTrip();
  TestAutoSixteenths();
  TestHitsLandOnTheSixteenth();
  TestTouchMotion();
  TestRandomAndScale();
  TestSixteenStepLoop();
  TestMotionState();
  TestNoStuckNotesFuzz();
  ReportCost();
  if (g_failures) {
    printf("%d FAILED\n", g_failures);
    return 1;
  }
  printf("PASS: engine tests\n");
  return 0;
}
