#include "fast_weights.h"

#include <immintrin.h>
#include <cstring>
#include <numeric>

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

FastWeightsEngine::FastWeightsEngine(int feat_dim, int vocab_size)
    : feat_dim_(feat_dim), vocab_size_(vocab_size), step_t_(0) {
  W_.assign(vocab_size_ * feat_dim_, 0.0f);
  b_.assign(vocab_size_, 0.0f);
  v_.assign(feat_dim_, 1.0f);
  h_prev_.assign(feat_dim_, 0.0f);
  logits_.assign(vocab_size_, 0.0f);
  lc_.assign(vocab_size_, 0.0f);
  probs_.assign(vocab_size_, 1.0f / (float)vocab_size_);
}

void FastWeightsEngine::Forward(const float* feat, const std::vector<bool>& vocab) {
  if (!feat) return;
  std::memcpy(h_prev_.data(), feat, feat_dim_ * sizeof(float));

  float max_lc = -1e9f;
  const int n_blocks = feat_dim_ / 8;

  for (int i = 0; i < vocab_size_; ++i) {
    if (i < (int)vocab.size() && !vocab[i]) {
      logits_[i] = -100.0f;
      lc_[i] = -15.0f;
      probs_[i] = 0.0f;
      continue;
    }

    const float* w_row = &W_[i * feat_dim_];
    __m256 acc = _mm256_setzero_ps();
    for (int b = 0; b < n_blocks; ++b) {
      __m256 vw = _mm256_loadu_ps(w_row + b * 8);
      __m256 vx = _mm256_loadu_ps(feat + b * 8);
      acc = _mm256_fmadd_ps(vw, vx, acc);
    }
    float dot = hsum256_ps(acc);
    for (int j = n_blocks * 8; j < feat_dim_; ++j) {
      dot += w_row[j] * feat[j];
    }
    float z = dot + b_[i];
    logits_[i] = z;

    // Softcapped logit: 15 * tanh(z / 15)
    float c = std::tanh(z * (1.0f / 15.0f));
    float lc = 15.0f * c;
    lc_[i] = lc;
    if (lc > max_lc) max_lc = lc;
  }

  // Softmax with max-subtraction
  float sum_exp = 0.0f;
  for (int i = 0; i < vocab_size_; ++i) {
    if (i < (int)vocab.size() && !vocab[i]) {
      probs_[i] = 0.0f;
      continue;
    }
    float e = std::exp(lc_[i] - max_lc);
    probs_[i] = e;
    sum_exp += e;
  }

  if (sum_exp > 0.0f) {
    float inv_sum = 1.0f / sum_exp;
    float sum_p = 0.0f;
    for (int i = 0; i < vocab_size_; ++i) {
      if (probs_[i] > 0.0f) {
        probs_[i] = std::max(probs_[i] * inv_sum, 1e-7f);
        sum_p += probs_[i];
      }
    }
    if (sum_p > 0.0f) {
      float renorm = 1.0f / sum_p;
      for (int i = 0; i < vocab_size_; ++i) {
        probs_[i] *= renorm;
      }
    }
  } else {
    float uniform_p = 1.0f / (float)vocab_size_;
    for (int i = 0; i < vocab_size_; ++i) {
      probs_[i] = (i < (int)vocab.size() && !vocab[i]) ? 0.0f : uniform_p;
    }
  }
}

void FastWeightsEngine::Update(uint8_t target_byte) {
  if (step_t_ == 0 && h_prev_[0] == 0.0f && h_prev_[1] == 0.0f) {
    // Initial warmup step prior to first forward pass
    return;
  }

  const int D = feat_dim_;
  const int n_blocks = D / 8;
  const float fb = 0.999f;
  const float one_minus_fb = 0.001f;

  // 1. FW-ROW running variance update
  float sum_sqrt_v = 0.0f;
  for (int j = 0; j < D; ++j) {
    float h = h_prev_[j];
    v_[j] = fb * v_[j] + one_minus_fb * (h * h);
    sum_sqrt_v += std::sqrt(v_[j]);
  }
  float msd = sum_sqrt_v / (float)D;

  // 2. Precondition feature vector sh
  std::vector<float> sh(D);
  float hsh = 0.0f;
  for (int j = 0; j < D; ++j) {
    float denom = std::sqrt(v_[j]) + 0.001f * msd + 1e-12f;
    float s = msd / denom;
    sh[j] = s * h_prev_[j];
    hsh += h_prev_[j] * sh[j];
  }

  // 3. Dynamic DIC learning rate schedule
  // kappa_t = kappa_f + (kappa_0 - kappa_f) / (1 + t / tau)
  constexpr float kappa_0 = 0.006f;
  constexpr float kappa_f = 0.0015f;
  constexpr float tau = 8000.0f;
  float kappa_t = kappa_f + (kappa_0 - kappa_f) / (1.0f + (float)step_t_ / tau);
  float eta = kappa_t / (1.0f + hsh);
  step_t_++;

  // 4. Surrogate cross-entropy backpropagation and rank-1 weight update
  const int y = (int)target_byte;
  for (int i = 0; i < vocab_size_; ++i) {
    float p = probs_[i];
    if (p <= 0.0f) continue;

    float d = p - (i == y ? 1.0f : 0.0f);
    float c = std::tanh(logits_[i] * (1.0f / 15.0f));
    float dl = d * (1.0f - c * c);
    float g = eta * dl;

    // Bias update
    b_[i] -= g;

    // Weight update: W[i, j] -= g * sh[j]
    float* w_row = &W_[i * D];
    __m256 vg = _mm256_set1_ps(g);
    for (int b = 0; b < n_blocks; ++b) {
      __m256 vw = _mm256_loadu_ps(w_row + b * 8);
      __m256 vsh = _mm256_loadu_ps(sh.data() + b * 8);
      vw = _mm256_fnmadd_ps(vg, vsh, vw);
      _mm256_storeu_ps(w_row + b * 8, vw);
    }
    for (int j = n_blocks * 8; j < D; ++j) {
      w_row[j] -= g * sh[j];
    }
  }
}

FastWeightsModel::FastWeightsModel(const std::vector<bool>& vocab, int feat_dim)
    : ByteModel(vocab), engine_(feat_dim, 256), feat_dim_(feat_dim) {}

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
