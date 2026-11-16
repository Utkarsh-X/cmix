#ifndef BYTE_MIXER_H
#define BYTE_MIXER_H

#include <vector>
#include <memory>

#include "../models/byte-model.h"
#include "lstm.h"

class ByteMixer : public ByteModel {
 public:
  ByteMixer(unsigned int num_models, const unsigned int& bit_context,
      const std::vector<bool>& vocab, unsigned int vocab_size, Lstm* lstm);
  void SetInput(int index, float val);
  void SetOutputBias(const float* bias256);
  void ByteUpdate();
  const std::valarray<float>& Hidden() const { return lstm_->Hidden(); }

 private:
  std::unique_ptr<Lstm> lstm_;
  const unsigned int& byte_;
  std::valarray<int> byte_map_;
  std::valarray<float> inputs_;
  std::valarray<float> folded_bias_;
  unsigned int num_models_, vocab_size_, offset_;
};

#include "../transformer.h"

class Tf1Slot : public ByteModel {
 public:
  explicit Tf1Slot(const std::vector<bool>& vocab) : ByteModel(vocab) {}
  void SetProbs(const float* set_probs);
  void SetProbsNeutral();
  void SetProbsOff();
};

#endif
