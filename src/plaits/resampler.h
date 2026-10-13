// oPlaits - 47872.34 Hz -> 44.1 kHz polyphase resampler for the Plaits engine.
// MIT.
//
// Plaits renders at the hardware's own frame rate (see plaits_engine.h for
// why, and scripts/gen_tables.py for the numbers). 44100 / 47872.34 is
// exactly 6909 / 7500, so output sample n sits at input time
// n * 7500 / 6909 = q + r / 6909. The 64-tap Kaiser-windowed sinc is
// tabulated at 256 fractional positions; a position between two rows is the
// linear interpolation of the two rows' outputs (the same thing as
// interpolating the coefficients, at half the multiplies). Flat to ~20 kHz,
// -6 dB at 22.05 kHz, >= 70 dB down from ~23.7 kHz.
//
// The input side is pull-driven: the caller asks NeedsInput() and pushes one
// Plaits block (12 samples) at a time until the next output can be formed.
#ifndef OPLAITS_RESAMPLER_H_
#define OPLAITS_RESAMPLER_H_

#include <stdint.h>
#include <string.h>

#include "oplaits_tables.h"

namespace oplaits {

class Resampler {
 public:
  static const uint32_t kRing = 256;  // > taps + one Plaits block, power of 2

  void Reset() {
    memset(ring_, 0, sizeof(ring_));
    // Pretend 2 * HALF zeros were written, so the first output (centred on
    // q = HALF) has a full window of history. Latency: HALF input samples
    // (0.67 ms) plus at most one Plaits block.
    written_ = 2 * OPLAITS_RS_HALF;
    q_ = OPLAITS_RS_HALF;
    r_ = 0;
  }

  // True while the next output needs input that has not been pushed yet.
  // Compared as a signed DIFFERENCE, so the counters wrapping after ~25 h
  // changes nothing.
  bool NeedsInput() const {
    return static_cast<int32_t>(written_ - q_) <= OPLAITS_RS_HALF;
  }

  void Push(float x) {
    ring_[written_ & (kRing - 1)] = x;
    ++written_;
  }

  // Only call when !NeedsInput().
  float Next() {
    const uint32_t scaled = r_ * OPLAITS_RS_PHASES;
    const uint32_t p = scaled / OPLAITS_RS_UP;
    const float f = static_cast<float>(scaled - p * OPLAITS_RS_UP) * (1.0f / OPLAITS_RS_UP);
    const float *h0 = oplaits_rs_table[p];
    const float *h1 = oplaits_rs_table[p + 1];
    const uint32_t base = q_ - OPLAITS_RS_HALF + 1;
    float a0 = 0.0f, a1 = 0.0f;
    for (int j = 0; j < OPLAITS_RS_TAPS; ++j) {
      const float x = ring_[(base + j) & (kRing - 1)];
      a0 += h0[j] * x;
      a1 += h1[j] * x;
    }
    r_ += OPLAITS_RS_DOWN;
    while (r_ >= OPLAITS_RS_UP) {
      r_ -= OPLAITS_RS_UP;
      ++q_;
    }
    return a0 + (a1 - a0) * f;
  }

 private:
  float ring_[kRing];
  uint32_t written_;  // input samples written (wraps harmlessly: unsigned)
  uint32_t q_;        // integer input time of the next output
  uint32_t r_;        // fractional part, in 1/6909 of an input sample
};

}  // namespace oplaits

#endif  // OPLAITS_RESAMPLER_H_
