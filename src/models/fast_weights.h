#ifndef FAST_WEIGHTS_H
#define FAST_WEIGHTS_H

#include <vector>
#include <valarray>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <memory>

#include "byte-model.h"

// EXP-071: Pure-Online Fast-Weights Linear Associative Memory (0 Checkpoint Bytes)
// Maintains an online rank-1 associative perturbation matrix W in R^{256 x D}
// initialized to all zeros. Updates per-byte via surrogate cross-entropy
// backpropagation with Normalized Least Mean Squares (NLMS) and FW-ROW
// per-feature RMS preconditioning.
class FastWeightsEngine {
 public:
  explicit FastWeightsEngine(int feat_dim = 200, int vocab_size = 256);

  // Computes logits = W * feat + b, applies softcap(15), and calculates
  // normalized softmax byte probabilities across valid vocab symbols.
  void Forward(const float* feat, const std::vector<bool>& vocab);

  // Executes online surrogate backpropagation given observed ground-truth byte:
  // dl = (p - 1_y) * (1 - tanh(z/15)^2)
  // sh = FW-ROW preconditioned features
  // W -= eta * dl (outer product) sh
  void Update(uint8_t target_byte);

  const std::vector<float>& Probs() const { return probs_; }
  float Prob(int o) const { return probs_[o]; }
  int FeatDim() const { return feat_dim_; }
  uint64_t StepCount() const { return step_t_; }

 private:
  int feat_dim_;
  int vocab_size_;
  uint64_t step_t_ = 0;

  // Synaptic weight matrix: row i is weights for symbol i (size: vocab_size_ * feat_dim_)
  // Strictly zero-initialized at process start -> 0 checkpoint bytes overhead!
  std::vector<float> W_;
  std::vector<float> b_;

  // Per-feature running RMS variance for FW-ROW preconditioning
  std::vector<float> v_;

  // Stored inputs from step t used for step t update
  std::vector<float> h_prev_;
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

  FastWeightsEngine& Engine() { return engine_; }
  const FastWeightsEngine& Engine() const { return engine_; }

 private:
  FastWeightsEngine engine_;
  int feat_dim_;
};

#endif  // FAST_WEIGHTS_H
