#include "fast_weights.h"

#include <immintrin.h>
#include <cstring>
#include <numeric>
#include <cmath>
#include <algorithm>

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
    : feat_dim_(feat_dim), vocab_size_(vocab_size), step_t_(0), avg_loss_(4.0f) {
  // Rank-4 Bottleneck matrices: U is V x 4, V_mat is 4 x D (contiguous rows)
  U_.assign(vocab_size_ * RANK, 0.0f);
  b_.assign(vocab_size_, 0.0f);
  V_mat_.assign(RANK * feat_dim_, 0.0f);

  // Procedural Orthonormal DCT-II initialization for V_mat (0 Checkpoint Bytes!)
  constexpr float pi = 3.14159265358979323846f;
  const float scale = std::sqrt(2.0f / (float)feat_dim_);
  for (int k = 0; k < RANK; ++k) {
    for (int j = 0; j < feat_dim_; ++j) {
      V_mat_[k * feat_dim_ + j] = scale * std::cos(pi * (j + 0.5f) * (k + 1) / (float)feat_dim_);
    }
  }

  // Adaptive Second-Moment & Momentum Accumulators
  m_U_.assign(vocab_size_ * RANK, 0.0f);
  v_U_.assign(vocab_size_ * RANK, 1e-4f);
  m_V_.assign(RANK * feat_dim_, 0.0f);
  v_V_.assign(RANK * feat_dim_, 1e-4f);

  // Feature statistics & activations
  v_feat_.assign(feat_dim_, 1.0f);
  h_prev_.assign(feat_dim_, 0.0f);
  q_prev_.assign(RANK, 0.0f);
  logits_.assign(vocab_size_, 0.0f);
  lc_.assign(vocab_size_, 0.0f);
  probs_.assign(vocab_size_, 1.0f / (float)vocab_size_);
}

void FastWeightsEngine::Forward(const float* feat, const std::vector<bool>& vocab) {
  if (!feat) return;
  std::memcpy(h_prev_.data(), feat, feat_dim_ * sizeof(float));

  const int n_blocks = feat_dim_ / 8;

  // 1. Bottleneck projection: q[k] = V_mat[k] dot feat in AVX2
  for (int k = 0; k < RANK; ++k) {
    const float* v_row = &V_mat_[k * feat_dim_];
    __m256 acc = _mm256_setzero_ps();
    for (int b = 0; b < n_blocks; ++b) {
      __m256 vv = _mm256_loadu_ps(v_row + b * 8);
      __m256 vx = _mm256_loadu_ps(feat + b * 8);
      acc = _mm256_fmadd_ps(vv, vx, acc);
    }
    float dot = hsum256_ps(acc);
    for (int j = n_blocks * 8; j < feat_dim_; ++j) {
      dot += v_row[j] * feat[j];
    }
    q_prev_[k] = dot;
  }

  // 2. Logits projection: z = U * q + b
  float max_lc = -1e9f;
  const float q0 = q_prev_[0], q1 = q_prev_[1], q2 = q_prev_[2], q3 = q_prev_[3];

  for (int i = 0; i < vocab_size_; ++i) {
    if (i < (int)vocab.size() && !vocab[i]) {
      logits_[i] = -100.0f;
      lc_[i] = -15.0f;
      probs_[i] = 0.0f;
      continue;
    }

    const float* u_row = &U_[i * RANK];
    float z = u_row[0] * q0 + u_row[1] * q1 + u_row[2] * q2 + u_row[3] * q3 + b_[i];
    logits_[i] = z;

    // Softcapped logit: 15 * tanh(z / 15)
    float c = std::tanh(z * (1.0f / 15.0f));
    float lc = 15.0f * c;
    lc_[i] = lc;
    if (lc > max_lc) max_lc = lc;
  }

  // 3. Softmax with max-subtraction
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
    return;
  }

  const int D = feat_dim_;
  const int n_blocks = D / 8;
  const int y = (int)target_byte;

  // 1. Surprise estimation & Friction modulation
  float target_p = probs_[target_byte];
  float loss = -std::log(std::max(target_p, 1e-7f));
  float surprise = std::max(0.0f, loss - avg_loss_);
  avg_loss_ = 0.98f * avg_loss_ + 0.02f * loss;

  // Base momentum mu0 = 0.85, drains to 0 on sudden surprise
  float mu = 0.85f * std::exp(-0.8f * surprise);
  constexpr float beta2 = 0.999f;
  constexpr float one_minus_beta2 = 0.001f;
  constexpr float eps = 1e-6f;

  // 2. Precondition feature vector sh via FW-ROW running variance
  const float fb = 0.999f;
  const float one_minus_fb = 0.001f;
  float sum_sqrt_v = 0.0f;
  for (int j = 0; j < D; ++j) {
    float h = h_prev_[j];
    v_feat_[j] = fb * v_feat_[j] + one_minus_fb * (h * h);
    sum_sqrt_v += std::sqrt(v_feat_[j]);
  }
  float msd = sum_sqrt_v / (float)D;

  std::vector<float> sh(D);
  float hsh = 0.0f;
  for (int j = 0; j < D; ++j) {
    float denom = std::sqrt(v_feat_[j]) + 0.001f * msd + 1e-12f;
    float s = msd / denom;
    sh[j] = s * h_prev_[j];
    hsh += h_prev_[j] * sh[j];
  }

  // 3. Dynamic DIC learning rate schedule
  constexpr float kappa_0 = 0.006f;
  constexpr float kappa_f = 0.0015f;
  constexpr float tau = 8000.0f;
  float kappa_t = kappa_f + (kappa_0 - kappa_f) / (1.0f + (float)step_t_ / tau);
  float eta = kappa_t / (1.0f + hsh * 0.05f);
  step_t_++;

  // 4. Compute logit errors dl and backpropagate error through bottleneck: delta_q = U^T * dl
  std::vector<float> dl(vocab_size_);
  float delta_q[RANK] = {0.0f, 0.0f, 0.0f, 0.0f};

  for (int i = 0; i < vocab_size_; ++i) {
    float p = probs_[i];
    if (p <= 0.0f) continue;

    float d = p - (i == y ? 1.0f : 0.0f);
    float c = std::tanh(logits_[i] * (1.0f / 15.0f));
    float err = d * (1.0f - c * c);
    dl[i] = err;

    // Bias update
    b_[i] -= eta * err;

    // Accumulate error into bottleneck
    const float* u_row = &U_[i * RANK];
    delta_q[0] += u_row[0] * err;
    delta_q[1] += u_row[1] * err;
    delta_q[2] += u_row[2] * err;
    delta_q[3] += u_row[3] * err;
  }

  // 5. Update U with Adam / Momentum + Second Moment
  const float q0 = q_prev_[0], q1 = q_prev_[1], q2 = q_prev_[2], q3 = q_prev_[3];
  for (int i = 0; i < vocab_size_; ++i) {
    float err = dl[i];
    if (err == 0.0f) continue;

    float* u_row = &U_[i * RANK];
    float* mu_row = &m_U_[i * RANK];
    float* vu_row = &v_U_[i * RANK];

    const float g[4] = {err * q0, err * q1, err * q2, err * q3};
    for (int k = 0; k < RANK; ++k) {
      mu_row[k] = mu * mu_row[k] + (1.0f - mu) * g[k];
      vu_row[k] = beta2 * vu_row[k] + one_minus_beta2 * (g[k] * g[k]);

      float step = eta * mu_row[k] / (std::sqrt(vu_row[k]) + eps);
      u_row[k] = std::clamp(u_row[k] - step, -10.0f, 10.0f);
    }
  }

  // 6. Update V_mat (RANK x D) with backpropagated bottleneck error delta_q in AVX2
  for (int k = 0; k < RANK; ++k) {
    float dq_k = delta_q[k];
    if (dq_k == 0.0f) continue;

    float* v_row = &V_mat_[k * D];
    float* mv_row = &m_V_[k * D];
    float* vv_row = &v_V_[k * D];

    for (int j = 0; j < D; ++j) {
      float g = dq_k * sh[j];
      mv_row[j] = mu * mv_row[j] + (1.0f - mu) * g;
      vv_row[j] = beta2 * vv_row[j] + one_minus_beta2 * (g * g);

      float step = eta * mv_row[j] / (std::sqrt(vv_row[j]) + eps);
      v_row[j] = std::clamp(v_row[j] - step, -10.0f, 10.0f);
    }
  }
}

void FastWeightsEngine::ResetMomentum() {
  std::fill(m_U_.begin(), m_U_.end(), 0.0f);
  std::fill(m_V_.begin(), m_V_.end(), 0.0f);
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
