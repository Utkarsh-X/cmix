#ifndef FAST_WEIGHTS_H
#define FAST_WEIGHTS_H

#include <vector>
#include <valarray>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <memory>
#include <immintrin.h>

#include "model.h"

// ============================================================================
// EXP-178: Disentangled Dual-Stream Fast-Weights Associative Memory Engine
// Separates memory heads into distinct timescales and feature spaces:
// - Head 1 (Persistent Syntactic Head): 400d LSTM activations, lambda_1 = 1.000
// - Head 2 (Burst Lexical Head): 256d Lexical features, lambda_2 = 0.985
// 0 Checkpoint Bytes Overhead. Purely online, deterministic & invertible.
// ============================================================================
class FastWeightsEngine {
 public:
  static constexpr int kSynFeatDim = 400;
  static constexpr int kLexFeatDim = 256;
  static constexpr int kVocabSize = 256;

  FastWeightsEngine();

  // Forward pass computing both syntactic and lexical associative logits,
  // applying 15*tanh(z/15) softcapping, and calculating normalized softmax byte distributions.
  void Forward(const float* feat_syn, const float* feat_lex, const std::vector<bool>& vocab);

  // Executes online surrogate backpropagation on ground-truth target byte:
  // Updates Head 1 and Head 2 independently with FW-ROW preconditioning and NLMS denominator.
  void Update(uint8_t target_byte);

  const std::vector<float>& Probs1() const { return probs1_; }
  const std::vector<float>& Probs2() const { return probs2_; }
  float Prob1(int o) const { return probs1_[o]; }
  float Prob2(int o) const { return probs2_[o]; }
  uint64_t StepCount() const { return step_t_; }

 private:
  uint64_t step_t_ = 0;

  // Stream 1: Persistent Syntactic Head (400d, lambda = 1.000)
  std::vector<float> W1_;       // 256 * 400 = 102,400 floats (400 KiB)
  std::vector<float> b1_;       // 256 floats (1 KiB)
  std::vector<float> v1_;       // 400 floats (1.56 KiB)
  std::vector<float> h1_prev_;  // 400 floats (1.56 KiB)
  std::vector<float> logits1_;  // 256 floats (1 KiB)
  std::vector<float> lc1_;      // 256 floats (1 KiB)
  std::vector<float> probs1_;   // 256 floats (1 KiB)
  std::vector<float> sh1_;      // 400 floats (1.56 KiB)

  // Stream 2: Burst Lexical Head (256d, lambda = 0.985)
  std::vector<float> W2_;       // 256 * 256 = 65,536 floats (256 KiB)
  std::vector<float> b2_;       // 256 floats (1 KiB)
  std::vector<float> v2_;       // 256 floats (1 KiB)
  std::vector<float> h2_prev_;  // 256 floats (1 KiB)
  std::vector<float> logits2_;  // 256 floats (1 KiB)
  std::vector<float> lc2_;      // 256 floats (1 KiB)
  std::vector<float> probs2_;   // 256 floats (1 KiB)
  std::vector<float> sh2_;      // 256 floats (1 KiB)
};

// Disentangled Dual-Output Model derived from Model(2) for CMIX mixer integration.
// outputs_[0] = Syntactic Head bit prediction
// outputs_[1] = Lexical Head bit prediction
class FastWeightsModel : public Model {
 public:
  explicit FastWeightsModel(const std::vector<bool>& vocab);

  // Updates associative memory with observed byte, then forward-projects next features
  void UpdateByte(uint8_t target_byte, const float* feat_syn, const float* feat_lex);

  const std::valarray<float>& Predict() override;
  void Perceive(int bit) override;
  void ByteUpdate() override;

  FastWeightsEngine& Engine() { return engine_; }
  const FastWeightsEngine& Engine() const { return engine_; }

 private:
  const std::vector<bool>& vocab_;
  FastWeightsEngine engine_;

  int top_ = 255;
  int mid_ = 0;
  int bot_ = 0;

  std::valarray<float> probs1_;
  std::valarray<float> probs2_;
};

#endif  // FAST_WEIGHTS_H
