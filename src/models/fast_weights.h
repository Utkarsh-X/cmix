#ifndef FAST_WEIGHTS_H
#define FAST_WEIGHTS_H

#include <vector>
#include <valarray>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <memory>
#include <immintrin.h>

#include "byte-model.h"

// ============================================================================
// EXP-167: FW-DUAL Pure-Online Fast-Weights Associative Memory Engine
// 0 Checkpoint Bytes Overhead. Fully reversible online learning.
// ============================================================================
class FastWeightsEngine {
 public:
  static constexpr int kDefaultFeatDim = 512;
  static constexpr int kVocabSize = 256;

  explicit FastWeightsEngine(int feat_dim = kDefaultFeatDim);

  // Computes associative logits = (W_long + W_short) * sh + b, applies 15*tanh(z/15),
  // and computes normalized softmax byte probabilities.
  void Forward(const float* feat, const std::vector<bool>& vocab);

  // Executes online surrogate backpropagation on ground-truth target byte:
  // 1. FW-ROW running variance EMA & feature preconditioning
  // 2. NLMS contractive denominator step computation
  // 3. Dual-rate outer product update to W_long and W_short
  void Update(uint8_t target_byte);

  const std::vector<float>& Probs() const { return probs_; }
  float Prob(int o) const { return probs_[o]; }
  int FeatDim() const { return feat_dim_; }
  uint64_t StepCount() const { return step_t_; }

 private:
  int feat_dim_;
  uint64_t step_t_ = 0;

  // Synaptic associative weight matrices (zero-initialized at startup)
  std::vector<float> W_long_;   // Persistent syntax memory (lambda = 1.0)
  std::vector<float> W_short_;  // Bursty recency memory (lambda = 0.990)
  std::vector<float> b_;        // Per-byte bias

  // FW-ROW per-feature second-moment variance running average
  std::vector<float> v_;

  // Cached scratchpad vectors for step t causal pairing
  std::vector<float> h_prev_;
  std::vector<float> logits_;
  std::vector<float> lc_;
  std::vector<float> probs_;
};

// Byte-to-bit slicing wrapper derived from ByteModel for CMIX mixer integration
class FastWeightsModel : public ByteModel {
 public:
  FastWeightsModel(const std::vector<bool>& vocab, int feat_dim = FastWeightsEngine::kDefaultFeatDim);

  // Updates associative memory with observed byte, then forward-projects next features
  void UpdateByte(uint8_t target_byte, const float* next_feat);

  void ByteUpdate() override;

  FastWeightsEngine& Engine() { return engine_; }
  const FastWeightsEngine& Engine() const { return engine_; }

 private:
  FastWeightsEngine engine_;
  int feat_dim_;
};

#endif  // FAST_WEIGHTS_H
