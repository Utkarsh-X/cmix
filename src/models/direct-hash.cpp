#include "direct-hash.h"

DirectHash::DirectHash(const unsigned long long& byte_context,
    const unsigned int& bit_context, int limit, float delta, int size) :
    byte_context_(byte_context), bit_context_(bit_context), index_(0),
    limit_(limit), delta_(delta), divisor_(1.0 / (limit + delta)),
    table_(size > 0 ? static_cast<size_t>(size) : 0),
    checksums_(table_.size()) {}

const std::valarray<float>& DirectHash::Predict() {
  outputs_[0] = table_.Predict(static_cast<size_t>(index_), bit_context_);
  return outputs_;
}

void DirectHash::Perceive(int bit) {
  table_.Perceive(static_cast<size_t>(index_), bit_context_, bit, limit_,
      delta_, divisor_);
}

void DirectHash::ByteUpdate() {
  const size_t n = table_.size();
  if (n == 0) return;
  index_ = byte_context_ % n;
  for (int i = 0; i < 20; ++i) {
    checksums_.EnsureElement(index_);
    if (checksums_[index_] == 0) {
      checksums_[index_] = byte_context_;
      break;
    }
    if (checksums_[index_] == byte_context_) break;
    if (i == 19) {
      table_.ResetIfReady(static_cast<size_t>(index_));
      checksums_[index_] = byte_context_;
      break;
    }
    ++index_;
    if (index_ == n) index_ = 0;
  }
}
