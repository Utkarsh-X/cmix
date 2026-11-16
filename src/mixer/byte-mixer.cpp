#include "byte-mixer.h"

ByteMixer::ByteMixer(unsigned int num_models, const unsigned int& bit_context,
    const std::vector<bool>& vocab, unsigned int vocab_size, Lstm* lstm) :
    ByteModel(vocab), lstm_(lstm), byte_(bit_context), byte_map_(0, 256),
    inputs_(0.0, vocab_size), folded_bias_(0.0f, vocab_size),
    num_models_(num_models), vocab_size_(vocab_size),
    offset_(0) {
  for (int i = 0; i < 256; ++i) {
    byte_map_[i] = offset_;
    if (vocab_[i]) ++offset_;
  }
  offset_ = 0;
}

void ByteMixer::SetInput(int index, float val) {
  if (!vocab_[index]) return;
  inputs_[offset_] += val;
  ++offset_;
  if (offset_ == vocab_size_) offset_ = 0;
}

void ByteMixer::SetOutputBias(const float* bias256) {
  for (int i = 0; i < 256; ++i) {
    if (vocab_[i]) folded_bias_[byte_map_[i]] = bias256[i];
  }
  lstm_->SetOutputBias(&folded_bias_[0]);
}

void ByteMixer::ByteUpdate() {
  inputs_ *= 2.0f / (float)num_models_;
  lstm_->SetInput(inputs_);
  inputs_ = 0;
  const auto& output = lstm_->Perceive(byte_map_[byte_]);
  offset_ = 0;
  for (int i = 0; i < 256; ++i) {
    if (vocab_[i]) {
      probs_[i] = output[offset_];
      ++offset_;
    } else {
      probs_[i] = 0;
    }
  }
  offset_ = 0;
  ByteModel::ByteUpdate();
}

void Tf1Slot::SetProbs(const float* set_probs) {
  for (int i = 0; i < 256; ++i) {
    probs_[i] = (vocab_[i] && transformer::InSet(i))
        ? set_probs[transformer::kByteToIndex[i]] : 0.0f;
  }
  ByteModel::ByteUpdate();
}

void Tf1Slot::SetProbsNeutral() {
  const float u = 1.0f / (float)transformer::kSetSize;
  for (int i = 0; i < 256; ++i) {
    probs_[i] = (vocab_[i] && transformer::InSet(i)) ? u : 0.0f;
  }
  ByteModel::ByteUpdate();
}

void Tf1Slot::SetProbsOff() {
  for (int i = 0; i < 256; ++i) probs_[i] = 0.0f;
  ByteModel::ByteUpdate();
}
