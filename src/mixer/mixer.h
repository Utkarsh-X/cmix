#ifndef MIXER_H
#define MIXER_H

#include <vector>
#include <valarray>
#include <unordered_map>
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
  float E2EP() const { return p_; }
  const float* E2EWRow();
  const float* E2EXRow();
  void E2ESetErr(float e) { e2e_err_ = e; e2e_use_ = true; }
  void E2ESeedHead(unsigned int head, float val);

 private:
  ContextData* GetContextData();
  const std::valarray<float>& inputs_;
  const std::vector<float>& extra_inputs_vec_;
  std::valarray<float> extra_inputs_;
  float p_, learning_rate_;
  const unsigned long long& context_;
  unsigned long long max_steps_, steps_;
  std::unordered_map<unsigned int, std::unique_ptr<ContextData>> context_map_;
  float e2e_err_ = 0.0f;
  bool e2e_use_ = false;
  ContextData* current_data_ = nullptr;
};

#endif
