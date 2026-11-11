#include "indirect.h"
#include <stdlib.h>

Indirect::Indirect(const State& state,
    const unsigned long long& byte_context,
    const unsigned int& bit_context, float delta,
    std::vector<unsigned char>& map) :  byte_context_(byte_context),
    bit_context_(bit_context), map_index_(0), map_offset_(0),
    divisor_(1.0 / delta), state_(state), map_(map) {
  div_map_.Init(map_.size() - 257);
  map_offset_ = div_map_.Mod(rand());
  for (int i = 0; i < 256; ++i) {
    predictions_[i] = state_.InitProbability(i);
  }
}

const std::valarray<float>& Indirect::Predict() {
  map_index_ += bit_context_;
  outputs_[0] = predictions_[map_[map_index_]];
  return outputs_;
}

void Indirect::Perceive(int bit) {
  int state = map_[map_index_];
  predictions_[state] += (bit - predictions_[state]) * divisor_;
  map_[map_index_] = state_.Next(state, bit);
  map_index_ -= bit_context_;
}

void Indirect::ByteUpdate() {
  map_index_ = div_map_.Mod(257 * byte_context_ + map_offset_);
  __builtin_prefetch(&map_[map_index_], 0, 3);
}
