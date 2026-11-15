#ifndef PPM_LOGIT_PRIOR_H_
#define PPM_LOGIT_PRIOR_H_

#include <math.h>
#include <stdint.h>
#include <string.h>

// PPMd prior on the output logits of the byte LSTM, taken from cmix-obias
// (David Freelan, GPL-3.0) in the form of its constant-weight build.
// At every byte boundary Row() turns the PPMd byte distribution p into
// bias = g * ln(max(p, 1e-6)), which the LSTM adds to its output logits
// before the softmax (ByteMixer::SetOutputBias and Lstm::SetOutputBias).
// The softmax output is also the error signal of the LSTM, so the network
// learns the residual against PPMd. The factor g is fixed here (kGate).
namespace ppm_logit_prior {

static constexpr float kGateInit  = 550 / 1000.0f; // 0.550
static constexpr float kGateFloor = 220 / 1000.0f; // 0.220
static constexpr float kTau       = 45000.0f;      // 45 KB scale
static constexpr float kFloor     = 1e-6f;
static constexpr float kLn2       = 0.69314718055994531f;

inline float GateForPos(uint64_t byte_pos) {
  return kGateFloor + (kGateInit - kGateFloor) * expf(-static_cast<float>(byte_pos) / kTau);
}

// bias256[i] = g(t) * ln(max(p256[i], 1e-6)) for all 256 byte values.
// The caller folds the row onto the vocabulary positions.
inline void Row(const float* p256, float* bias256, uint64_t byte_pos = 0) {
  const float kG = GateForPos(byte_pos);
  for (int i = 0; i < 256; ++i) {
    float pv = p256[i];
    if (!(pv > kFloor)) pv = kFloor;   // clamps 0, subnormals and NaN
    bias256[i] = kG * logf(pv);
  }
}

}  // namespace ppm_logit_prior

#endif
