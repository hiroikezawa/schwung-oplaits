// oPlaits - the official voice, driven by a fixed pseudo-random performance,
// written raw to stdout. scripts/compare_upstream.sh builds this twice --
// against the vendored (patched) tree and against an untouched upstream
// checkout -- and compares the two outputs byte for byte, which is what
// shows what patches/ change. MIT.
//
// Every engine; notes with velocities up to 127 (accent 1.0, the input
// patch 0001 is about), legato retriggers, releases, the LEVEL and TRIG
// inputs patched and unpatched, all knobs swept over their whole range, and
// engine switches.
#include <stdio.h>
#include <string.h>

#include <new>

#include "plaits/dsp/voice.h"
#include "stmlib/utils/buffer_allocator.h"
#include "stmlib/utils/random.h"

alignas(plaits::Voice) static unsigned char g_mem[sizeof(plaits::Voice)];
alignas(16) static char g_ram[16384];

int main() {
  memset(g_mem, 0, sizeof(g_mem));
  plaits::Voice *v = new (g_mem) plaits::Voice();
  stmlib::BufferAllocator allocator(g_ram, sizeof(g_ram));
  v->Init(&allocator);
  stmlib::Random::Seed(0x21);

  plaits::Patch p;
  plaits::Modulations m;
  memset(&p, 0, sizeof(p));
  memset(&m, 0, sizeof(m));
  p.engine = 8;
  p.harmonics = p.timbre = p.morph = 0.5f;
  p.decay = 0.5f;
  m.trigger_patched = true;

  unsigned r = 12345;
  auto next = [&]() { r = r * 1664525u + 1013904223u; return r; };
  auto unit = [&]() { return (next() >> 8) / 16777216.0f; };
  plaits::Voice::Frame f[12];
  for (int engine = 0; engine < 24; ++engine) {
    p.engine = engine;
    for (int event = 0; event < 120; ++event) {
      switch (next() % 10) {
        case 0: case 1:   // note on (legato retrigger when the gate is high)
          m.note = static_cast<float>(24 + next() % 72);
          m.level = static_cast<float>(1 + next() % 127) / 127.0f;
          if (next() % 4 == 0) m.level = 1.0f;
          if (m.trigger > 0.5f) {
            m.trigger = 0.0f;
            v->Render(p, m, f, 12);
            fwrite(f, sizeof(f), 1, stdout);
          }
          m.trigger = 1.0f;
          break;
        case 2: m.trigger = 0.0f; m.level = 0.0f; break;
        case 3: p.harmonics = unit(); break;
        case 4: p.timbre = unit(); break;
        case 5: p.morph = unit(); break;
        case 6: p.decay = unit(); p.lpg_colour = unit(); break;
        case 7:
          p.frequency_modulation_amount = 2.0f * unit() - 1.0f;
          p.timbre_modulation_amount = 2.0f * unit() - 1.0f;
          p.morph_modulation_amount = 2.0f * unit() - 1.0f;
          break;
        case 8:
          m.trigger_patched = next() % 4 != 0;
          m.level_patched = next() % 2 == 0;
          break;
        default:
          if (next() % 6 == 0) p.engine = static_cast<int>(next() % 24);
          else p.engine = engine;
          break;
      }
      const int blocks = 1 + static_cast<int>(next() % 120);
      // The log scripts/compare_upstream.sh classifies differences with:
      // "<frame offset> <engine rendering from here>".
      fprintf(stderr, "%ld %d\n", ftell(stdout) / 4, p.engine);
      for (int b = 0; b < blocks; ++b) {
        v->Render(p, m, f, 12);
        fwrite(f, sizeof(f), 1, stdout);
      }
    }
  }
  return 0;
}
