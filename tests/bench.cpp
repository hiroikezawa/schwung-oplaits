// oPlaits - CPU benchmark, run ON THE MOVE (scripts/bench_move.sh). MIT.
//
// Per engine: the mean and the worst time to render one 128-frame block
// (2.9 ms of audio at 44.1 kHz) with a note held, plus the worst block that
// contains an engine switch. Built with the module's own flags, so the
// numbers are the plugin's.
#include <stdio.h>
#include <time.h>

#include "wrapper/oplaits.h"

using namespace oplaits;

static OPlaits g_synth;

static double Now() {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

int main() {
  static const char *const kNames[kNumEngines] = {
      "VAVCF", "PHDIS", "6OP1", "6OP2", "6OP3", "WTERR", "STRMC", "CHIP",
      "VA", "WSHAP", "FM", "GRAIN", "ADDIT", "WTBL", "CHORD", "SPEAK",
      "SWARM", "NOISE", "PARTI", "STRNG", "MODAL", "KICK", "SNARE", "HIHAT"};
  const double kBudget = 128.0 / 44100.0 * 1e6;
  float buf[128];
  double worst_all = 0.0, mean_max = 0.0;
  printf("engine        mean us  max us  (block = %.0f us)\n", kBudget);
  for (int e = 0; e < kNumEngines; ++e) {
    g_synth.Init(1u);
    g_synth.SetParam(P_ENGINE, static_cast<float>(e));
    g_synth.SetParam(P_TRIG, 1);     // free running + LEVEL held: always sounding
    g_synth.SetParam(P_VELO, 1);
    const uint8_t on[3] = {0x90, 48, 100};
    g_synth.Midi(on, 3);
    for (int i = 0; i < 50; ++i) g_synth.Process(buf, 128, -1.0, 120.0f, true);
    double sum = 0.0, worst = 0.0;
    const int n = 3000;
    for (int i = 0; i < n; ++i) {
      // Knobs move as a hand would: every block, slowly.
      g_synth.SetParam(P_TIMBRE, (i % 1000) / 1000.0f);
      g_synth.SetParam(P_MORPH, 1.0f - (i % 700) / 700.0f);
      g_synth.SetParam(P_HARMONICS, (i % 1300) / 1300.0f);
      const double a = Now();
      g_synth.Process(buf, 128, -1.0, 120.0f, true);
      const double d = Now() - a;
      sum += d;
      if (d > worst) worst = d;
    }
    printf("%2d %-5s     %7.1f %7.1f\n", e, kNames[e], sum / n, worst);
    if (worst > worst_all) worst_all = worst;
    if (sum / n > mean_max) mean_max = sum / n;
  }
  // Engine switches: every block a different engine, notes retriggered.
  g_synth.Init(1u);
  double sw = 0.0;
  for (int i = 0; i < 2000; ++i) {
    g_synth.SetParam(P_ENGINE, static_cast<float>((i * 7) % kNumEngines));
    const uint8_t on[3] = {0x90, static_cast<uint8_t>(36 + i % 24), 100};
    g_synth.Midi(on, 3);
    const double a = Now();
    g_synth.Process(buf, 128, -1.0, 120.0f, true);
    const double d = Now() - a;
    if (d > sw) sw = d;
  }
  // Instance creation (create_instance runs Init on the SPI callback).
  double init = 0.0;
  for (int i = 0; i < 50; ++i) {
    const double a = Now();
    g_synth.Init(static_cast<uint32_t>(i));
    const double d = Now() - a;
    if (d > init) init = d;
  }
  printf("heaviest engine mean %.1f us = %.1f %% of a block\n", mean_max, 100.0 * mean_max / kBudget);
  printf("worst block %.1f us; worst block with an engine switch %.1f us; Init %.1f us\n", worst_all, sw, init);
  return 0;
}
