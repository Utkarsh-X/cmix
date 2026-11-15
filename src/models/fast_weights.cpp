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

FastWeightsEngine::FastWeightsEngine() : step_t_(0) {
  // Stream 1: Syntactic Head (400d, lambda = 1.000)
  W1_.assign(kVocabSize * kSynFeatDim, 0.0f);
  b1_.assign(kVocabSize, 0.0f);
  v1_.assign(kSynFeatDim, 1.0f);
  h1_prev_.assign(kSynFeatDim, 0.0f);
  logits1_.assign(kVocabSize, 0.0f);
  lc1_.assign(kVocabSize, 0.0f);
  probs1_.assign(kVocabSize, 1.0f / (float)kVocabSize);
  sh1_.assign(kSynFeatDim, 0.0f);

  // Stream 2: Burst Lexical Head (256d, lambda = 0.985)
  W2_.assign(kVocabSize * kLexFeatDim, 0.0f);
  b2_.assign(kVocabSize, 0.0f);
  v2_.assign(kLexFeatDim, 1.0f);
  h2_prev_.assign(kLexFeatDim, 0.0f);
  logits2_.assign(kVocabSize, 0.0f);
  lc2_.assign(kVocabSize, 0.0f);
  probs2_.assign(kVocabSize, 1.0f / (float)kVocabSize);
  sh2_.assign(kLexFeatDim, 0.0f);
}

void FastWeightsEngine::Forward(const float* feat_syn, const float* feat_lex, const std::vector<bool>& vocab) {
  if (!feat_syn || !feat_lex) return;
  std::memcpy(h1_prev_.data(), feat_syn, kSynFeatDim * sizeof(float));
  std::memcpy(h2_prev_.data(), feat_lex, kLexFeatDim * sizeof(float));

  // --------------------------------------------------------------------------
  // 1. Forward Pass Head 1: Syntactic (400d)
  // --------------------------------------------------------------------------
  float max_lc1 = -1e9f;
  for (int i = 0; i < kVocabSize; ++i) {
    if (i < (int)vocab.size() && !vocab[i]) {
      logits1_[i] = -100.0f;
      lc1_[i] = -15.0f;
      probs1_[i] = 0.0f;
      continue;
    }

    const float* w1_row = &W1_[i * kSynFeatDim];
    __m256 acc0 = _mm256_setzero_ps();
    __m256 acc1 = _mm256_setzero_ps();

    for (int j = 0; j < kSynFeatDim; j += 16) {
      __m256 vw0 = _mm256_loadu_ps(w1_row + j);
      __m256 vx0 = _mm256_loadu_ps(feat_syn + j);
      acc0 = _mm256_fmadd_ps(vw0, vx0, acc0);

      __m256 vw1 = _mm256_loadu_ps(w1_row + j + 8);
      __m256 vx1 = _mm256_loadu_ps(feat_syn + j + 8);
      acc1 = _mm256_fmadd_ps(vw1, vx1, acc1);
    }
    float dot1 = hsum256_ps(_mm256_add_ps(acc0, acc1));
    float z1 = dot1 + b1_[i];
    logits1_[i] = z1;

    // Softcapped logit: 15 * tanh(z / 15)
    float lc1 = 15.0f * std::tanh(z1 * (1.0f / 15.0f));
    lc1_[i] = lc1;
    if (lc1 > max_lc1) max_lc1 = lc1;
  }

  // Softmax with maximum subtraction for numerical stability
  float sum_exp1 = 0.0f;
  for (int i = 0; i < kVocabSize; ++i) {
    if (i < (int)vocab.size() && !vocab[i]) {
      probs1_[i] = 0.0f;
      continue;
    }
    float e = std::exp(lc1_[i] - max_lc1);
    probs1_[i] = e;
    sum_exp1 += e;
  }

  if (sum_exp1 > 0.0f) {
    float inv_sum1 = 1.0f / sum_exp1;
    float sum_p1 = 0.0f;
    for (int i = 0; i < kVocabSize; ++i) {
      if (probs1_[i] > 0.0f) {
        probs1_[i] = std::max(probs1_[i] * inv_sum1, 1e-7f);
        sum_p1 += probs1_[i];
      }
    }
    if (sum_p1 > 0.0f) {
      float renorm1 = 1.0f / sum_p1;
      for (int i = 0; i < kVocabSize; ++i) probs1_[i] *= renorm1;
    }
  } else {
    float uniform = 1.0f / (float)kVocabSize;
    for (int i = 0; i < kVocabSize; ++i) {
      probs1_[i] = (i < (int)vocab.size() && !vocab[i]) ? 0.0f : uniform;
    }
  }

  // --------------------------------------------------------------------------
  // 2. Forward Pass Head 2: Burst Lexical (256d)
  // --------------------------------------------------------------------------
  float max_lc2 = -1e9f;
  for (int i = 0; i < kVocabSize; ++i) {
    if (i < (int)vocab.size() && !vocab[i]) {
      logits2_[i] = -100.0f;
      lc2_[i] = -15.0f;
      probs2_[i] = 0.0f;
      continue;
    }

    const float* w2_row = &W2_[i * kLexFeatDim];
    __m256 acc0 = _mm256_setzero_ps();
    __m256 acc1 = _mm256_setzero_ps();

    for (int j = 0; j < kLexFeatDim; j += 16) {
      __m256 vw0 = _mm256_loadu_ps(w2_row + j);
      __m256 vx0 = _mm256_loadu_ps(feat_lex + j);
      acc0 = _mm256_fmadd_ps(vw0, vx0, acc0);

      __m256 vw1 = _mm256_loadu_ps(w2_row + j + 8);
      __m256 vx1 = _mm256_loadu_ps(feat_lex + j + 8);
      acc1 = _mm256_fmadd_ps(vw1, vx1, acc1);
    }
    float dot2 = hsum256_ps(_mm256_add_ps(acc0, acc1));
    float z2 = dot2 + b2_[i];
    logits2_[i] = z2;

    float lc2 = 15.0f * std::tanh(z2 * (1.0f / 15.0f));
    lc2_[i] = lc2;
    if (lc2 > max_lc2) max_lc2 = lc2;
  }

  float sum_exp2 = 0.0f;
  for (int i = 0; i < kVocabSize; ++i) {
    if (i < (int)vocab.size() && !vocab[i]) {
      probs2_[i] = 0.0f;
      continue;
    }
    float e = std::exp(lc2_[i] - max_lc2);
    probs2_[i] = e;
    sum_exp2 += e;
  }

  if (sum_exp2 > 0.0f) {
    float inv_sum2 = 1.0f / sum_exp2;
    float sum_p2 = 0.0f;
    for (int i = 0; i < kVocabSize; ++i) {
      if (probs2_[i] > 0.0f) {
        probs2_[i] = std::max(probs2_[i] * inv_sum2, 1e-7f);
        sum_p2 += probs2_[i];
      }
    }
    if (sum_p2 > 0.0f) {
      float renorm2 = 1.0f / sum_p2;
      for (int i = 0; i < kVocabSize; ++i) probs2_[i] *= renorm2;
    }
  } else {
    float uniform = 1.0f / (float)kVocabSize;
    for (int i = 0; i < kVocabSize; ++i) {
      probs2_[i] = (i < (int)vocab.size() && !vocab[i]) ? 0.0f : uniform;
    }
  }
}

void FastWeightsEngine::Update(uint8_t target_byte) {
  if (step_t_ == 0 && h1_prev_[0] == 0.0f && h1_prev_[1] == 0.0f && h2_prev_[0] == 0.0f) {
    return;  // Cold start warmup
  }

  const float fb = 0.999f;
  const float one_minus_fb = 0.001f;
  const int y = (int)target_byte;

  // --------------------------------------------------------------------------
  // 1. FW-ROW Preconditioning & NLMS Schedule for Head 1 (Syntactic, 400d)
  // --------------------------------------------------------------------------
  float sum_sqrt_v1 = 0.0f;
  for (int j = 0; j < kSynFeatDim; ++j) {
    float h = h1_prev_[j];
    v1_[j] = fb * v1_[j] + one_minus_fb * (h * h);
    sum_sqrt_v1 += std::sqrt(v1_[j]);
  }
  float msd1 = sum_sqrt_v1 / (float)kSynFeatDim;

  float hsh1 = 0.0f;
  for (int j = 0; j < kSynFeatDim; ++j) {
    float denom = std::sqrt(v1_[j]) + 0.001f * msd1 + 1e-12f;
    float s = msd1 / denom;
    sh1_[j] = s * h1_prev_[j];
    hsh1 += h1_prev_[j] * sh1_[j];
  }

  // Head 1 Learning rate schedule:
  // kappa_{1,t} = 0.0015 + 0.0045 / (1 + t / 8000)
  constexpr float kappa_0 = 0.0060f;
  constexpr float kappa_f = 0.0015f;
  constexpr float tau = 8000.0f;
  float kappa_1t = kappa_f + (kappa_0 - kappa_f) / (1.0f + (float)step_t_ / tau);
  float eta1 = kappa_1t / (1.0f + hsh1);

  // --------------------------------------------------------------------------
  // 2. FW-ROW Preconditioning & NLMS Schedule for Head 2 (Burst Lexical, 256d)
  // --------------------------------------------------------------------------
  float sum_sqrt_v2 = 0.0f;
  for (int j = 0; j < kLexFeatDim; ++j) {
    float h = h2_prev_[j];
    v2_[j] = fb * v2_[j] + one_minus_fb * (h * h);
    sum_sqrt_v2 += std::sqrt(v2_[j]);
  }
  float msd2 = sum_sqrt_v2 / (float)kLexFeatDim;

  float hsh2 = 0.0f;
  for (int j = 0; j < kLexFeatDim; ++j) {
    float denom = std::sqrt(v2_[j]) + 0.001f * msd2 + 1e-12f;
    float s = msd2 / denom;
    sh2_[j] = s * h2_prev_[j];
    hsh2 += h2_prev_[j] * sh2_[j];
  }

  // Fast-adapting burst rate: 1.75x base schedule
  float kappa_2t = kappa_1t * 1.75f;
  float eta2 = kappa_2t / (1.0f + hsh2);

  step_t_++;

  // --------------------------------------------------------------------------
  // 3. Disentangled Rank-1 Outer Product Updates
  // --------------------------------------------------------------------------
  // Head 1 (Syntactic, lambda_1 = 1.000 persistent document-level syntax)
  for (int i = 0; i < kVocabSize; ++i) {
    float p1 = probs1_[i];
    if (p1 <= 0.0f) continue;

    float d1 = p1 - (i == y ? 1.0f : 0.0f);
    float c1 = std::tanh(logits1_[i] * (1.0f / 15.0f));
    float dl1 = d1 * (1.0f - c1 * c1);
    float g1 = eta1 * dl1;

    b1_[i] -= g1;

    float* w1_row = &W1_[i * kSynFeatDim];
    __m256 vg1 = _mm256_set1_ps(g1);
    for (int j = 0; j < kSynFeatDim; j += 8) {
      __m256 vsh = _mm256_loadu_ps(sh1_.data() + j);
      __m256 vw = _mm256_loadu_ps(w1_row + j);
      vw = _mm256_fnmadd_ps(vg1, vsh, vw);
      _mm256_storeu_ps(w1_row + j, vw);
    }
  }

  // Head 2 (Burst Lexical, lambda_2 = 0.985 exponential forgetting)
  constexpr float lambda2 = 0.985f;
  __m256 v_decay2 = _mm256_set1_ps(lambda2);

  for (int i = 0; i < kVocabSize; ++i) {
    float p2 = probs2_[i];
    if (p2 <= 0.0f) continue;

    float d2 = p2 - (i == y ? 1.0f : 0.0f);
    float c2 = std::tanh(logits2_[i] * (1.0f / 15.0f));
    float dl2 = d2 * (1.0f - c2 * c2);
    float g2 = eta2 * dl2;

    b2_[i] = lambda2 * b2_[i] - g2;

    float* w2_row = &W2_[i * kLexFeatDim];
    __m256 vg2 = _mm256_set1_ps(g2);
    for (int j = 0; j < kLexFeatDim; j += 8) {
      __m256 vsh = _mm256_loadu_ps(sh2_.data() + j);
      __m256 vw = _mm256_loadu_ps(w2_row + j);
      vw = _mm256_mul_ps(vw, v_decay2);
      vw = _mm256_fnmadd_ps(vg2, vsh, vw);
      _mm256_storeu_ps(w2_row + j, vw);
    }
  }
}

FastWeightsModel::FastWeightsModel(const std::vector<bool>& vocab)
    : Model(2), vocab_(vocab), probs1_(1.0f / 256.0f, 256), probs2_(1.0f / 256.0f, 256) {}

void FastWeightsModel::UpdateByte(uint8_t target_byte, const float* feat_syn, const float* feat_lex) {
  engine_.Update(target_byte);
  engine_.Forward(feat_syn, feat_lex, vocab_);
  for (int i = 0; i < 256; ++i) {
    probs1_[i] = engine_.Prob1(i);
    probs2_[i] = engine_.Prob2(i);
  }
  ByteUpdate();
}

const std::valarray<float>& FastWeightsModel::Predict() {
  int mid = bot_ + ((top_ - bot_) / 2);

  // Stream 1: Syntactic Head bit prediction
  const float* base1 = &probs1_[0];
  float num1 = std::accumulate(base1 + mid + 1, base1 + top_ + 1, 0.0f);
  float denom1 = std::accumulate(base1 + bot_, base1 + mid + 1, num1);
  outputs_[0] = (denom1 == 0.0f) ? 0.5f : (num1 / denom1);

  // Stream 2: Burst Lexical Head bit prediction
  const float* base2 = &probs2_[0];
  float num2 = std::accumulate(base2 + mid + 1, base2 + top_ + 1, 0.0f);
  float denom2 = std::accumulate(base2 + bot_, base2 + mid + 1, num2);
  outputs_[1] = (denom2 == 0.0f) ? 0.5f : (num2 / denom2);

  return outputs_;
}

void FastWeightsModel::Perceive(int bit) {
  mid_ = bot_ + ((top_ - bot_) / 2);
  if (bit) {
    bot_ = mid_ + 1;
  } else {
    top_ = mid_;
  }
}

void FastWeightsModel::ByteUpdate() {
  top_ = 255;
  bot_ = 0;
  for (int i = 0; i < 256; ++i) {
    if (!vocab_[i]) {
      probs1_[i] = 0.0f;
      probs2_[i] = 0.0f;
    }
  }
}
