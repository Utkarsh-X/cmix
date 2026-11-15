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
// EXP-174: Quad-Block Multi-Scale Fast-Weights Associative Memory Engine
// 4 multi-scale temporal horizons:
// - Block 0: Ultra-fast recency cache (lambda = 0.920, tau_1/2 = 8.3 B, alpha = 2.50x)
// - Block 1: Short phrasal horizon (lambda = 0.985, tau_1/2 = 45.9 B, alpha = 1.75x)
// - Block 2: Medium discourse horizon (lambda = 0.997, tau_1/2 = 230.7 B, alpha = 1.25x)
// - Block 3: Long persistent memory (lambda = 1.000, tau_1/2 = inf, alpha = 1.00x)
// Total 1024 dims = 4 blocks x 256 dims. Total synaptic matrix memory = 1.0 MiB.
// ============================================================================
class FastWeightsEngine {
 public:
  static constexpr int kNumBlocks = 4;
  static constexpr int kBlockDim = 256;
  static constexpr int kTotalFeatDim = kNumBlocks * kBlockDim;  // 1024
  static constexpr int kVocabSize = 256;

  explicit FastWeightsEngine(int feat_dim = kTotalFeatDim);

  // Computes associative logits = b + sum_{b=0..3} W_b * h_b, applies 15*tanh(z/15),
  // and computes normalized softmax byte probabilities.
  void Forward(const float* feat, const std::vector<bool>& vocab);

  // Executes online surrogate backpropagation on ground-truth target byte:
  // 1. FW-ROW independent per-block running variance EMA & preconditioning
  // 2. Multi-block NLMS contractive denominator step computation
  // 3. Quad-rate outer product update to W_0, W_1, W_2, W_3 with scale-invariant tau=65536
  void Update(uint8_t target_byte);

  const std::vector<float>& Probs() const { return probs_; }
  float Prob(int o) const { return probs_[o]; }
  int FeatDim() const { return feat_dim_; }
  uint64_t StepCount() const { return step_t_; }

 private:
  int feat_dim_;
  uint64_t step_t_ = 0;

  // Quad-block synaptic associative weight matrices (zero-initialized at startup)
  // Each block matrix is kVocabSize x kBlockDim = 256 x 256 floats (256 KiB)
  std::vector<float> W_[kNumBlocks];
  std::vector<float> b_;  // Per-byte bias (256 floats)

  // FW-ROW per-block running second-moment variance running average (256 floats each)
  std::vector<float> v_[kNumBlocks];

  // Cached scratchpad vectors for step t causal pairing
  std::vector<float> h_prev_;  // 1024 floats
  std::vector<float> logits_;  // 256 floats
  std::vector<float> lc_;      // 256 floats
  std::vector<float> probs_;   // 256 floats
};

// Byte-to-bit slicing wrapper derived from ByteModel for CMIX mixer integration
class FastWeightsModel : public ByteModel {
 public:
  FastWeightsModel(const std::vector<bool>& vocab, int feat_dim = FastWeightsEngine::kTotalFeatDim);

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
