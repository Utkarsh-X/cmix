#ifndef FAST_WEIGHTS_H
#define FAST_WEIGHTS_H

#include <vector>
#include <valarray>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <memory>

#include "byte-model.h"

// EXP-078: Adaptive Online Learning & Momentum for Associative Memory (0 Checkpoint Bytes)
// Implements an asymmetric rank-4 associative bottleneck (W = U * V^T) where
// V in R^{D x 4} is procedurally initialized to an orthonormal DCT-II basis (0 checkpoint bytes)
// and U in R^{256 x 4} is zero-initialized. Features online diagonal second-moment
// preconditioning (AVX2 Adam/RMSProp) and surprise-gated momentum with boundary friction.
class FastWeightsEngine {
 public:
  static constexpr int RANK = 4;

  explicit FastWeightsEngine(int feat_dim = 200, int vocab_size = 256);

  // Computes latent bottleneck q = V^T * feat (AVX2), logits z = U * q + b, applies softcap(15),
  // and calculates normalized softmax byte probabilities across valid vocab symbols.
  void Forward(const float* feat, const std::vector<bool>& vocab);

  // Executes online surrogate backpropagation with surprise-gated momentum and diagonal second-moments:
  // dl = (p - 1_y) * (1 - tanh(z/15)^2)
  // delta_q = U^T * dl (backpropagated latent error)
  // Updates U and V with AVX2 vectorized Adam / RMSProp updates.
  void Update(uint8_t target_byte);

  void ResetMomentum();

  const std::vector<float>& Probs() const { return probs_; }
  float Prob(int o) const { return probs_[o]; }
  int FeatDim() const { return feat_dim_; }
  uint64_t StepCount() const { return step_t_; }

 private:
  int feat_dim_;
  int vocab_size_;
  uint64_t step_t_ = 0;
  float avg_loss_ = 4.0f;

  // Rank-4 Bottleneck: U is V x 4, V_mat is D x 4
  std::vector<float> U_;      // V x RANK
  std::vector<float> b_;      // V
  std::vector<float> V_mat_;  // D x RANK

  // Adaptive Second-Moment & Momentum Accumulators
  std::vector<float> m_U_, v_U_;
  std::vector<float> m_V_, v_V_;

  // Feature statistics & activations
  std::vector<float> v_feat_;
  std::vector<float> h_prev_;
  std::vector<float> q_prev_;
  std::vector<float> logits_;
  std::vector<float> lc_;
  std::vector<float> probs_;
};

// Byte-to-bit slicing wrapper derived from ByteModel
class FastWeightsModel : public ByteModel {
 public:
  FastWeightsModel(const std::vector<bool>& vocab, int feat_dim = 200);

  // Called to update weights with observed byte, then compute new distribution
  void UpdateByte(uint8_t target_byte, const float* next_feat);

  // ByteUpdate override (resets bit interval [0, 255])
  void ByteUpdate() override;

  void ResetMomentum() { engine_.ResetMomentum(); }

  FastWeightsEngine& Engine() { return engine_; }
  const FastWeightsEngine& Engine() const { return engine_; }

 private:
  FastWeightsEngine engine_;
  int feat_dim_;
};

#endif  // FAST_WEIGHTS_H
