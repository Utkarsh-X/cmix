#include "mixer.h"

#include "sigmoid.h"

#include <numeric>
#include <math.h>

#ifdef __AVX2__
#include <immintrin.h>

namespace {

inline float HorizontalSum(__m256 v) {
  __m128 hi = _mm256_extractf128_ps(v, 1);
  __m128 lo = _mm256_castps256_ps128(v);
  lo = _mm_add_ps(lo, hi);
  hi = _mm_movehl_ps(hi, lo);
  lo = _mm_add_ps(lo, hi);
  hi = _mm_shuffle_ps(lo, lo, 1);
  lo = _mm_add_ss(lo, hi);
  return _mm_cvtss_f32(lo);
}

inline float AvxDotProduct(const float* a, const float* b, int n) {
  __m256 sum = _mm256_setzero_ps();
  int i = 0;
  for (; i + 7 < n; i += 8) {
    __m256 va = _mm256_loadu_ps(a + i);
    __m256 vb = _mm256_loadu_ps(b + i);
    sum = _mm256_fmadd_ps(va, vb, sum);
  }
  float result = HorizontalSum(sum);
  for (; i < n; ++i) {
    result += a[i] * b[i];
  }
  return result;
}

// weights -= update * inputs
inline void AvxWeightUpdate(float* weights, const float* inputs, float update,
    int n) {
  __m256 vupdate = _mm256_set1_ps(update);
  int i = 0;
  for (; i + 7 < n; i += 8) {
    __m256 vw = _mm256_loadu_ps(weights + i);
    __m256 vi = _mm256_loadu_ps(inputs + i);
    vw = _mm256_fnmadd_ps(vupdate, vi, vw);
    _mm256_storeu_ps(weights + i, vw);
  }
  for (; i < n; ++i) {
    weights[i] -= update * inputs[i];
  }
}

// weights *= scale
inline void AvxScale(float* weights, float scale, int n) {
  __m256 vs = _mm256_set1_ps(scale);
  int i = 0;
  for (; i + 7 < n; i += 8) {
    __m256 vw = _mm256_loadu_ps(weights + i);
    vw = _mm256_mul_ps(vw, vs);
    _mm256_storeu_ps(weights + i, vw);
  }
  for (; i < n; ++i) {
    weights[i] *= scale;
  }
}

template <int R>
inline void DotRowsExact(const float* __restrict x, float* const* __restrict w,
    int n, float* __restrict out) {
  __m256 acc[R];
  for (int r = 0; r < R; ++r) acc[r] = _mm256_setzero_ps();
  int i = 0;
  for (; i + 7 < n; i += 8) {
    const __m256 xv = _mm256_loadu_ps(x + i);
    for (int r = 0; r < R; ++r) {
      acc[r] = _mm256_fmadd_ps(xv, _mm256_loadu_ps(w[r] + i), acc[r]);
    }
  }
  for (int r = 0; r < R; ++r) out[r] = HorizontalSum(acc[r]);
  for (; i < n; ++i) {
    const float xv = x[i];
    for (int r = 0; r < R; ++r) out[r] += xv * w[r][i];
  }
}

template <int R>
inline void TrainRowsExact(const float* __restrict x, float* const* __restrict w,
    int n, const float* __restrict u) {
  __m256 uv[R];
  for (int r = 0; r < R; ++r) uv[r] = _mm256_set1_ps(u[r]);
  int i = 0;
  for (; i + 7 < n; i += 8) {
    const __m256 xv = _mm256_loadu_ps(x + i);
    for (int r = 0; r < R; ++r) {
      __m256 vw = _mm256_loadu_ps(w[r] + i);
      vw = _mm256_fnmadd_ps(uv[r], xv, vw);
      _mm256_storeu_ps(w[r] + i, vw);
    }
  }
  for (; i < n; ++i) {
    const float xv = x[i];
    for (int r = 0; r < R; ++r) w[r][i] -= u[r] * xv;
  }
}

}  // namespace
#endif

Mixer::Mixer(const std::valarray<float>& inputs,
    const std::vector<float>& extra_inputs,
    const unsigned long long& context, float learning_rate,
    unsigned int extra_input_size) : inputs_(inputs),
    extra_inputs_vec_(extra_inputs), extra_inputs_(extra_input_size), p_(0.5),
    learning_rate_(learning_rate), context_(context), max_steps_(1), steps_(0)
    {}

inline ContextData* Mixer::GetContextData() {
  const unsigned int key = static_cast<unsigned int>(context_);
  if (key == cached_key_ && cached_ptr_ != nullptr) return cached_ptr_;
  ContextData* data = ResolveContextSlow(key);
  cached_key_ = key;
  cached_ptr_ = data;
  return data;
}

ContextData* Mixer::ResolveContextSlow(unsigned int key) {
  auto it = context_map_.find(key);
  if (it != context_map_.end()) return it->second;

  constexpr unsigned long long limit = 10000;
  if (context_map_.size() >= limit) {
    auto dead = context_map_.find(0xDEADBEEFu);
    if (dead != context_map_.end()) return dead->second;
    context_pool_.emplace_back(
        new ContextData(inputs_.size(), extra_inputs_.size()));
    ContextData* ptr = context_pool_.back().get();
    context_map_.emplace(0xDEADBEEFu, ptr);
    return ptr;
  }
  context_pool_.emplace_back(
      new ContextData(inputs_.size(), extra_inputs_.size()));
  ContextData* ptr = context_pool_.back().get();
  context_map_.emplace(key, ptr);
  return ptr;
}

float* Mixer::PrepareMix() {
  ContextData* data = GetContextData();
  current_data_ = data;
  return &data->weights[0];
}

float Mixer::FinishMix(float main_dot) {
  ContextData* data = current_data_;
  p_ = main_dot;

  for (unsigned int i = 0; i < extra_inputs_.size(); ++i) {
    extra_inputs_[i] = extra_inputs_vec_[i];
  }

#ifdef __AVX2__
  int ne = extra_inputs_.size();
  if (ne > 0) {
    p_ += AvxDotProduct(&extra_inputs_[0], &data->extra_weights[0], ne);
  }
#else
  float e = 0;
  for (unsigned int i = 0; i < extra_inputs_.size(); ++i) {
    e += extra_inputs_[i] * data->extra_weights[i];
  }
  p_ += e;
#endif

  return p_;
}

float Mixer::Mix() {
  float* w = PrepareMix();
#ifdef __AVX2__
  int n = inputs_.size();
  float main_dot = AvxDotProduct(&inputs_[0], w, n);
#else
  float main_dot = 0;
  for (unsigned int i = 0; i < inputs_.size(); ++i) {
    main_dot += inputs_[i] * w[i];
  }
#endif
  return FinishMix(main_dot);
}

void Mixer::BatchDot(const float* inputs, float* const* weights, int count,
    int n, float* out_dots) {
#ifdef __AVX2__
  int r = 0;
  for (; r + 3 < count; r += 4) {
    DotRowsExact<4>(inputs, weights + r, n, out_dots + r);
  }
  for (; r < count; ++r) {
    out_dots[r] = AvxDotProduct(inputs, weights[r], n);
  }
#else
  for (int r = 0; r < count; ++r) {
    float p = 0;
    const float* w = weights[r];
    for (int i = 0; i < n; ++i) p += inputs[i] * w[i];
    out_dots[r] = p;
  }
#endif
}

void Mixer::BatchTrain(const float* inputs, float* const* weights, int count,
    int n, const float* updates) {
#ifdef __AVX2__
  int r = 0;
  for (; r + 3 < count; r += 4) {
    TrainRowsExact<4>(inputs, weights + r, n, updates + r);
  }
  for (; r < count; ++r) {
    AvxWeightUpdate(weights[r], inputs, updates[r], n);
  }
#else
  for (int r = 0; r < count; ++r) {
    float* w = weights[r];
    const float u = updates[r];
    for (int i = 0; i < n; ++i) w[i] -= u * inputs[i];
  }
#endif
}

const float* Mixer::E2EWRow() {
  if (!current_data_) current_data_ = GetContextData();
  return &current_data_->weights[0];
}

const float* Mixer::E2EXRow() {
  if (extra_inputs_.size() == 0) return nullptr;
  if (!current_data_) current_data_ = GetContextData();
  return &current_data_->extra_weights[0];
}

void Mixer::E2ESeedHead(unsigned int head, float val) {
  ContextData* d = GetContextData();
  for (unsigned int i = 0; i < head && i < d->weights.size(); ++i) {
    d->weights[i] = val;
  }
}

float Mixer::ComputeUpdate(int bit) {
  ContextData* data = current_data_ ? current_data_ : GetContextData();
  current_data_ = data;
  float decay = 0.9 / pow(0.0000001 * steps_ + 0.8, 0.8);
  decay *= 1.5 - ((1.0 * data->steps) / max_steps_);

  // Per-Row Learning rate adaptation (PRL)
  float row_decay = 0.10f;  // Ex-Girlfriend mixdecay::kFinal = 0.100
  if (data->steps < 25000) {
    row_decay = 0.3f;
    if (data->steps < 5000) {
      row_decay = 0.7f;
      if (data->steps < 1000) {
        row_decay = 1.0f;
      }
    }
  }
  if (row_decay > decay) decay = row_decay;

  float err = e2e_use_ ? e2e_err_ : (Sigmoid::Logistic(p_) - bit);
  e2e_use_ = false;

  // Study 18: Confidence-Gated Deadband Updates (epsilon = 0.010)
  // When model is already >99% confident, freeze gradient push to prevent weight saturation.
  float abs_err = (err >= 0.0f) ? err : -err;
  float update = (abs_err > 0.010f) ? (decay * learning_rate_ * err) : 0.0f;

  ++steps_;
  ++data->steps;
  if (data->steps > max_steps_) {
    max_steps_ = data->steps;
  }

  // Study 18: Passive L2 Weight Shrinkage (lambda = 3.0e-5)
  // Regulates weight norm over long sequences (1MB), preserving model plasticity.
  constexpr float lambda = 3.0e-5f;
  float shrinkage = 1.0f - lambda * decay;

  int ne = extra_inputs_.size();
  if (ne > 0) {
#ifdef __AVX2__
    if (shrinkage < 1.0f) AvxScale(&data->extra_weights[0], shrinkage, ne);
    if (update != 0.0f) AvxWeightUpdate(&data->extra_weights[0], &extra_inputs_[0], update, ne);
#else
    if (shrinkage < 1.0f) data->extra_weights *= shrinkage;
    if (update != 0.0f) data->extra_weights -= update * extra_inputs_;
#endif
  }
  return update;
}

void Mixer::Perceive(int bit) {
  float update = ComputeUpdate(bit);
  ContextData* data = current_data_;
  current_data_ = nullptr;

  float decay = 0.9 / pow(0.0000001 * steps_ + 0.8, 0.8);
  constexpr float lambda = 3.0e-5f;
  float shrinkage = 1.0f - lambda * decay;

#ifdef __AVX2__
  int n = inputs_.size();
  if (shrinkage < 1.0f) AvxScale(&data->weights[0], shrinkage, n);
  if (update != 0.0f) AvxWeightUpdate(&data->weights[0], &inputs_[0], update, n);
#else
  if (shrinkage < 1.0f) data->weights *= shrinkage;
  if (update != 0.0f) data->weights -= update * inputs_;
#endif
}
