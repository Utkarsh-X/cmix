#ifndef LINEAR_ATTENTION_H
#define LINEAR_ATTENTION_H

#include <vector>
#include <valarray>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <memory>
#include <immintrin.h>

#include "byte-model.h"

namespace {

struct AttentionPRNG {
  uint64_t s;
  explicit AttentionPRNG(uint64_t seed = 0x5E1FA77E0192BEEFULL) : s(seed) {}
  inline uint64_t NextU64() {
    uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  inline float NextFloat() {
    return (NextU64() >> 40) * (1.0f / 16777216.0f); // [0, 1)
  }
  inline float NextGaussian() {
    float u1 = std::max(NextFloat(), 1e-7f);
    float u2 = NextFloat();
    return std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
  }
};

} // namespace

template <int D = 64>
class OnlineLinearAttentionEngine {
 public:
  OnlineLinearAttentionEngine(int input_dim = 400, int vocab_size = 256, float lambda = 0.9995f)
      : in_dim_(input_dim), vocab_size_(vocab_size), lambda_(lambda) {
    AttentionPRNG rng(0x5E1FA77E0192BEEFULL);
    // Deterministic random Fourier feature projections (0 checkpoint bytes)
    Omega_k_.resize(D * in_dim_);
    Omega_q_.resize(D * in_dim_);
    for (float& w : Omega_k_) w = rng.NextGaussian() * (1.0f / std::sqrt((float)in_dim_));
    for (float& w : Omega_q_) w = rng.NextGaussian() * (1.0f / std::sqrt((float)in_dim_));
    beta_k_.resize(D);
    beta_q_.resize(D);
    for (float& b : beta_k_) b = rng.NextFloat() * 6.2831853f;
    for (float& b : beta_q_) b = rng.NextFloat() * 6.2831853f;

    // Zero-initialized associative memory state (16 KB in L1 cache)
    S_.assign(D * D, 0.0f);
    phi_k_.assign(D, 0.0f);
    phi_q_.assign(D, 0.0f);
    attn_out_.assign(D, 0.0f);

    // Online classification head: mapping attention vector (D=64) to vocab (256)
    W_head_.assign(vocab_size_ * D, 0.0f);
    b_head_.assign(vocab_size_, 0.0f);
    logits_.assign(vocab_size_, 0.0f);
    lc_.assign(vocab_size_, 0.0f);
    probs_.assign(vocab_size_, 1.0f / (float)vocab_size_);
  }

  // Projects input features to RFF space, queries S, and produces softmax probabilities
  void Forward(const float* feat, const std::vector<bool>& vocab) {
    const float norm = std::sqrt(2.0f / (float)D);
    // Compute phi_q and phi_k from input features
    for (int i = 0; i < D; ++i) {
      float dot_q = 0.0f;
      float dot_k = 0.0f;
      const float* Wq_row = &Omega_q_[i * in_dim_];
      const float* Wk_row = &Omega_k_[i * in_dim_];
      for (int j = 0; j < in_dim_; ++j) {
        dot_q += Wq_row[j] * feat[j];
        dot_k += Wk_row[j] * feat[j];
      }
      phi_q_[i] = norm * std::cos(dot_q + beta_q_[i]);
      phi_k_[i] = norm * std::cos(dot_k + beta_k_[i]);
    }

    // Query attention memory: attn_out = phi_q * S
    const int blocks = D / 8;
    for (int j = 0; j < D; ++j) attn_out_[j] = 0.0f;
    for (int i = 0; i < D; ++i) {
      __m256 qi = _mm256_set1_ps(phi_q_[i]);
      const float* S_row = &S_[i * D];
      for (int j = 0; j < blocks; ++j) {
        __m256 vo = _mm256_loadu_ps(&attn_out_[j * 8]);
        __m256 s = _mm256_loadu_ps(&S_row[j * 8]);
        vo = _mm256_fmadd_ps(qi, s, vo);
        _mm256_storeu_ps(&attn_out_[j * 8], vo);
      }
    }

    // Head logits: logits = W_head * attn_out + b_head
    float max_lc = -1e9f;
    for (int i = 0; i < vocab_size_; ++i) {
      if (!vocab[i]) {
        probs_[i] = 0.0f;
        continue;
      }
      const float* Wh_row = &W_head_[i * D];
      __m256 acc = _mm256_setzero_ps();
      for (int j = 0; j < blocks; ++j) {
        __m256 va = _mm256_loadu_ps(&attn_out_[j * 8]);
        __m256 vw = _mm256_loadu_ps(&Wh_row[j * 8]);
        acc = _mm256_fmadd_ps(va, vw, acc);
      }
      // Horizontal sum
      __m128 hi = _mm256_extractf128_ps(acc, 1);
      __m128 lo = _mm256_castps256_ps128(acc);
      lo = _mm_add_ps(lo, hi);
      hi = _mm_movehl_ps(hi, lo);
      lo = _mm_add_ps(lo, hi);
      hi = _mm_shuffle_ps(lo, lo, 1);
      lo = _mm_add_ss(lo, hi);
      float dot = _mm_cvtss_f32(lo) + b_head_[i];
      logits_[i] = dot;
      float lc = 15.0f * std::tanh(dot / 15.0f);
      lc_[i] = lc;
      if (lc > max_lc) max_lc = lc;
    }

    // Softmax
    float sum_exp = 0.0f;
    for (int i = 0; i < vocab_size_; ++i) {
      if (vocab[i]) {
        probs_[i] = std::exp(lc_[i] - max_lc);
        sum_exp += probs_[i];
      }
    }
    float inv_sum = (sum_exp > 1e-12f) ? (1.0f / sum_exp) : 0.0f;
    for (int i = 0; i < vocab_size_; ++i) {
      probs_[i] *= inv_sum;
    }
  }

  // Updates associative memory state S and linear head via Delta-Rule
  void Update(uint8_t target_byte) {
    ++step_t_;
    const int blocks = D / 8;
    alignas(32) float v_target[D];
    for (int j = 0; j < D; ++j) v_target[j] = 0.0f;
    v_target[target_byte % D] = 1.0f;

    // Delta-Rule: v_hat = phi_k * S
    alignas(32) float v_hat[D];
    alignas(32) float e[D];
    for (int j = 0; j < D; ++j) v_hat[j] = 0.0f;

    for (int i = 0; i < D; ++i) {
      __m256 ki = _mm256_set1_ps(phi_k_[i]);
      const float* S_row = &S_[i * D];
      for (int j = 0; j < blocks; ++j) {
        __m256 vh = _mm256_loadu_ps(&v_hat[j * 8]);
        __m256 s = _mm256_loadu_ps(&S_row[j * 8]);
        vh = _mm256_fmadd_ps(ki, s, vh);
        _mm256_storeu_ps(&v_hat[j * 8], vh);
      }
    }

    // Error e = eta * (v_hat - v_target)
    float eta = 0.25f / (1.0f + 0.0001f * (float)step_t_);
    __m256 veta = _mm256_set1_ps(eta);
    for (int j = 0; j < blocks; ++j) {
      __m256 vh = _mm256_loadu_ps(&v_hat[j * 8]);
      __m256 vt = _mm256_loadu_ps(&v_target[j * 8]);
      __m256 err = _mm256_mul_ps(_mm256_sub_ps(vh, vt), veta);
      _mm256_storeu_ps(&e[j * 8], err);
    }

    // S = lambda * S - phi_k^T * e
    __m256 vlam = _mm256_set1_ps(lambda_);
    for (int i = 0; i < D; ++i) {
      __m256 ki = _mm256_set1_ps(phi_k_[i]);
      float* S_row = &S_[i * D];
      for (int j = 0; j < blocks; ++j) {
        __m256 s = _mm256_loadu_ps(&S_row[j * 8]);
        __m256 ej = _mm256_loadu_ps(&e[j * 8]);
        s = _mm256_mul_ps(s, vlam);
        s = _mm256_fnmadd_ps(ki, ej, s);
        _mm256_storeu_ps(&S_row[j * 8], s);
      }
    }

    // Head SGD Update: surrogate cross-entropy
    float lr_head = 0.004f / (1.0f + (float)step_t_ / 10000.0f);
    for (int i = 0; i < vocab_size_; ++i) {
      float y = (i == target_byte) ? 1.0f : 0.0f;
      float tanh_val = std::tanh(logits_[i] / 15.0f);
      float d_softcap = 1.0f - tanh_val * tanh_val;
      float dl = (probs_[i] - y) * d_softcap;
      float update = lr_head * dl;

      b_head_[i] -= update;
      float* Wh_row = &W_head_[i * D];
      __m256 vup = _mm256_set1_ps(update);
      for (int j = 0; j < blocks; ++j) {
        __m256 vw = _mm256_loadu_ps(&Wh_row[j * 8]);
        __m256 va = _mm256_loadu_ps(&attn_out_[j * 8]);
        vw = _mm256_fnmadd_ps(vup, va, vw);
        _mm256_storeu_ps(&Wh_row[j * 8], vw);
      }
    }
  }

  const std::vector<float>& Probs() const { return probs_; }
  float Prob(int o) const { return probs_[o]; }

 private:
  int in_dim_;
  int vocab_size_;
  float lambda_;
  uint64_t step_t_ = 0;

  std::vector<float> Omega_k_, Omega_q_, beta_k_, beta_q_;
  std::vector<float> S_;
  std::vector<float> phi_k_, phi_q_, attn_out_;

  std::vector<float> W_head_, b_head_;
  std::vector<float> logits_, lc_, probs_;
};

class OnlineLinearAttentionModel : public ByteModel {
 public:
  OnlineLinearAttentionModel(const std::vector<bool>& vocab, int in_dim = 400)
      : ByteModel(vocab), engine_(in_dim, 256) {}

  void UpdateByte(uint8_t target_byte, const float* next_feat) {
    engine_.Update(target_byte);
    engine_.Forward(next_feat, vocab_);
    for (int i = 0; i < 256; ++i) {
      probs_[i] = engine_.Prob(i);
    }
    ByteModel::ByteUpdate();
  }

  void ByteUpdate() override {
    ByteModel::ByteUpdate();
  }

  OnlineLinearAttentionEngine<64>& Engine() { return engine_; }
  const OnlineLinearAttentionEngine<64>& Engine() const { return engine_; }

 private:
  OnlineLinearAttentionEngine<64> engine_;
};

#endif // LINEAR_ATTENTION_H
