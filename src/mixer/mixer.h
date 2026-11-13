#ifndef MIXER_H
#define MIXER_H

#include "../ds/emhash_map.hpp"

#include <vector>
#include <valarray>
#include <memory>

struct ContextData {
  ContextData(unsigned long long input_size,
      unsigned long long extra_input_size) : steps(0), weights(input_size),
      extra_weights(extra_input_size) {};
  unsigned long long steps;
  std::valarray<float> weights, extra_weights;
};

class Mixer {
 public:
  Mixer(const std::valarray<float>& inputs,
      const std::vector<float>& extra_inputs, const unsigned long long& context,
      float learning_rate, unsigned int extra_input_size);
  float Mix();
  void Perceive(int bit);
  float* PrepareMix();
  float FinishMix(float main_dot);
  float ComputeUpdate(int bit);
  float* CurrentWeights() { return &current_data_->weights[0]; }
  void ClearCurrent() { current_data_ = nullptr; }
  static void BatchDot(const float* inputs, float* const* weights, int count,
      int n, float* out_dots);
  static void BatchTrain(const float* inputs, float* const* weights, int count,
      int n, const float* updates);
  float E2EP() const { return p_; }
  const float* E2EWRow();
  const float* E2EXRow();
  void E2ESetErr(float e) { e2e_err_ = e; e2e_use_ = true; }
  void E2ESeedHead(unsigned int head, float val);

 private:
  inline ContextData* GetContextData();
  ContextData* ResolveContextSlow(unsigned int key);
  const std::valarray<float>& inputs_;
  const std::vector<float>& extra_inputs_vec_;
  std::valarray<float> extra_inputs_;
  float p_, learning_rate_;
  const unsigned long long& context_;
  unsigned long long max_steps_, steps_;
  emhash6::HashMap<unsigned int, ContextData*> context_map_;
  std::vector<std::unique_ptr<ContextData>> context_pool_;
  unsigned int cached_key_ = 0;
  ContextData* cached_ptr_ = nullptr;
  float e2e_err_ = 0.0f;
  bool e2e_use_ = false;
  ContextData* current_data_ = nullptr;
  float cached_decay_base_ = 1.0758f;
  float fast_err2_ = 0.25f;
  float slow_err2_ = 0.25f;
};

#endif
