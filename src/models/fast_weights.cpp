#include "fast_weights.h"

#include <cstring>
#include <numeric>

namespace {

// Fast horizontal sum for 256-bit AVX2 register
static inline float hsum256_ps(__m256 v) {
  __m128 vlow = _mm256_castps256_ps128(v);
  __m128 vhigh = _mm256_extractf128_ps(v, 1);
  __m128 sum128 = _mm_add_ps(vlow, vhigh);
  __m128 shuf = _mm_movehl_ps(sum128, sum128);
  __m128 sums = _mm_add_ps(sum128, shuf);
  shuf = _mm_shuffle_ps(sums, sums, 1);
  sums = _mm_add_ss(sums, shuf);
  return _mm_cvtss_f32(sums);
}

}  // namespace

FastWeightsEngine::FastWeightsEngine(int feat_dim)
    : feat_dim_(feat_dim), step_t_(0) {
  for (int b = 0; b < kNumBlocks; ++b) {
    W_[b].assign(kVocabSize * kBlockDim, 0.0f);
    v_[b].assign(kBlockDim, 1.0f);
  }
  b_.assign(kVocabSize, 0.0f);
  h_prev_.assign(kTotalFeatDim, 0.0f);
  logits_.assign(kVocabSize, 0.0f);
  lc_.assign(kVocabSize, 0.0f);
  probs_.assign(kVocabSize, 1.0f / (float)kVocabSize);
}

void FastWeightsEngine::Forward(const float* feat, const std::vector<bool>& vocab) {
  if (!feat) return;
  std::memcpy(h_prev_.data(), feat, kTotalFeatDim * sizeof(float));

  float max_lc = -1e9f;

  for (int i = 0; i < kVocabSize; ++i) {
    if (i < (int)vocab.size() && !vocab[i]) {
      logits_[i] = -100.0f;
      lc_[i] = -15.0f;
      probs_[i] = 0.0f;
      continue;
    }

    float z = b_[i];

    // Multi-block dot product: z = b_i + sum_{b=0..3} dot(W_b[i, :], h_b)
    for (int b = 0; b < kNumBlocks; ++b) {
      const float* w_row = &W_[b][i * kBlockDim];
      const float* hb = feat + b * kBlockDim;
      __m256 acc0 = _mm256_setzero_ps();
      __m256 acc1 = _mm256_setzero_ps();

      for (int j = 0; j < kBlockDim; j += 16) {
        __m256 vw0 = _mm256_loadu_ps(w_row + j);
        __m256 vx0 = _mm256_loadu_ps(hb + j);
        acc0 = _mm256_fmadd_ps(vw0, vx0, acc0);

        __m256 vw1 = _mm256_loadu_ps(w_row + j + 8);
        __m256 vx1 = _mm256_loadu_ps(hb + j + 8);
        acc1 = _mm256_fmadd_ps(vw1, vx1, acc1);
      }
      z += hsum256_ps(_mm256_add_ps(acc0, acc1));
    }

    logits_[i] = z;

    // Softcapped logit: 15 * tanh(z / 15)
    float c = 15.0f * std::tanh(z * (1.0f / 15.0f));
    lc_[i] = c;
    if (c > max_lc) max_lc = c;
  }

  // Numerically stable softmax
  float sum_exp = 0.0f;
  for (int i = 0; i < kVocabSize; ++i) {
    if (i < (int)vocab.size() && !vocab[i]) {
      probs_[i] = 0.0f;
      continue;
    }
    float ep = std::exp(lc_[i] - max_lc);
    probs_[i] = ep;
    sum_exp += ep;
  }
  float inv_sum = 1.0f / sum_exp;
  for (int i = 0; i < kVocabSize; ++i) {
    probs_[i] *= inv_sum;
  }
}

void FastWeightsEngine::Update(uint8_t target_byte) {
  if (step_t_ == 0 && h_prev_[0] == 0.0f && h_prev_[1] == 0.0f) {
    return;  // Cold start warmup
  }

  constexpr float fb = 0.999f;
  constexpr float one_minus_fb = 0.001f;

  // 1. FW-ROW Independent Per-Block Running Variance EMA & Preconditioning
  alignas(32) float sh[kTotalFeatDim];
  float hsh = 0.0f;

  for (int b = 0; b < kNumBlocks; ++b) {
    const float* hb = &h_prev_[b * kBlockDim];
    float* vb = &v_[b][0];
    float* sh_b = &sh[b * kBlockDim];

    float sum_sqrt_v = 0.0f;
    for (int j = 0; j < kBlockDim; ++j) {
      float h = hb[j];
      vb[j] = fb * vb[j] + one_minus_fb * (h * h);
      sum_sqrt_v += std::sqrt(vb[j]);
    }
    float msd = sum_sqrt_v * (1.0f / (float)kBlockDim);

    for (int j = 0; j < kBlockDim; ++j) {
      float denom = std::sqrt(vb[j]) + 0.001f * msd + 1e-12f;
      float s = msd / denom;
      if (s < 0.25f) s = 0.25f;
      else if (s > 4.0f) s = 4.0f;
      sh_b[j] = s * hb[j];
      hsh += hb[j] * sh_b[j];
    }
  }

  // 2. Dynamic DIC learning rate schedule with scale-invariant horizon tau = 65536
  constexpr float kappa_0 = 0.0060f;
  constexpr float kappa_f = 0.0015f;
  constexpr float tau = 65536.0f;
  float kappa_t = kappa_f + (kappa_0 - kappa_f) / (1.0f + (float)step_t_ / tau);
  float eta = kappa_t / (1.0f + hsh);
  step_t_++;

  // Multi-scale block decay rates and learning rate multipliers
  constexpr float kLambda[kNumBlocks] = {0.920f, 0.985f, 0.997f, 1.000f};
  constexpr float kAlpha[kNumBlocks]  = {2.50f,  1.75f,  1.25f,  1.00f};

  const int y = (int)target_byte;

  for (int i = 0; i < kVocabSize; ++i) {
    float p = probs_[i];
    if (p <= 0.0f) continue;

    // Canonical link Straight-Through Estimator: dl = p - 1[i == y]
    float dl = p - (i == y ? 1.0f : 0.0f);

    // Bias update
    b_[i] -= eta * dl;

    // Quad-block synaptic rank-1 updates
    for (int b = 0; b < kNumBlocks; ++b) {
      float* w_row = &W_[b][i * kBlockDim];
      const float* sh_b = &sh[b * kBlockDim];
      float g_b = (eta * kAlpha[b]) * dl;
      float lambda_b = kLambda[b];

      __m256 v_lambda = _mm256_set1_ps(lambda_b);
      __m256 v_gb = _mm256_set1_ps(g_b);

      if (lambda_b == 1.000f) {
        // Pure rank-1 update without decay
        for (int j = 0; j < kBlockDim; j += 8) {
          __m256 vw = _mm256_loadu_ps(w_row + j);
          __m256 vsh = _mm256_loadu_ps(sh_b + j);
          vw = _mm256_fnmadd_ps(v_gb, vsh, vw);
          _mm256_storeu_ps(w_row + j, vw);
        }
      } else {
        // Decaying rank-1 update: w = lambda * w - g * sh
        for (int j = 0; j < kBlockDim; j += 8) {
          __m256 vw = _mm256_loadu_ps(w_row + j);
          __m256 vsh = _mm256_loadu_ps(sh_b + j);
          vw = _mm256_mul_ps(vw, v_lambda);
          vw = _mm256_fnmadd_ps(v_gb, vsh, vw);
          _mm256_storeu_ps(w_row + j, vw);
        }
      }
    }
  }
}

// ============================================================================
// FastWeightsModel Bit-Slicing Wrapper Implementation
// ============================================================================
FastWeightsModel::FastWeightsModel(const std::vector<bool>& vocab, int feat_dim)
    : ByteModel(vocab), engine_(feat_dim), feat_dim_(feat_dim) {}

void FastWeightsModel::UpdateByte(uint8_t target_byte, const float* next_feat) {
  engine_.Update(target_byte);
  engine_.Forward(next_feat, vocab_);
  for (int i = 0; i < 256; ++i) {
    probs_[i] = engine_.Prob(i);
  }
  ByteModel::ByteUpdate();
}

void FastWeightsModel::ByteUpdate() {
  ByteModel::ByteUpdate();
}
