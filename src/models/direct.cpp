#include "direct.h"

Direct::Direct(const unsigned long long& byte_context,
    const unsigned int& bit_context, int limit, float delta, int size) :
    byte_context_(byte_context), bit_context_(bit_context), limit_(limit),
    delta_(delta), divisor_(1.0 / (limit + delta)),
    table_(size > 0 ? static_cast<size_t>(size) : 0) {}

const std::valarray<float>& Direct::Predict() {
  outputs_[0] = table_.Predict(static_cast<size_t>(byte_context_), bit_context_);
  return outputs_;
}

void Direct::Perceive(int bit) {
  table_.Perceive(static_cast<size_t>(byte_context_), bit_context_, bit,
      limit_, delta_, divisor_);
}
