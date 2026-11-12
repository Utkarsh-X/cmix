#include "bracket-context.h"
#include <algorithm>

static inline unsigned int BracketType(unsigned char c) {
  switch (c) {
    case '(': return 0;
    case '{': return 1;
    case '[': return 2;
    case '<': return 3;
    default:  return 0;
  }
}

BracketContext::BracketContext(const unsigned int& bit_context,
    int distance_limit, int stack_limit) : byte_(bit_context),
    distance_limit_(distance_limit), stack_limit_(stack_limit) {
  brackets_ = {{'(',')'}, {'{','}'}, {'[',']'}, {'<','>'}};
  context_ = 0;
  // Dense context space: 1 (empty) + 4 (bracket types) * 4 (depth levels) * distance_limit_
  size_ = 1 + 16 * distance_limit_;
}

void BracketContext::Update() {
  if (!active_.empty()) {
    if (brackets_[active_[active_.size() - 1]] == byte_ ||
        distance_[distance_.size() - 1] >= distance_limit_ - 1) {
      active_.pop_back();
      distance_.pop_back();
    } else {
      ++distance_[distance_.size() - 1];
    }
  }
  if (brackets_.find(byte_) != brackets_.end()) {
    active_.push_back(byte_);
    distance_.push_back(0);
    // Fix unbounded stack bug: check active_.size() instead of brackets_.size()
    if (active_.size() > stack_limit_) {
      active_.erase(active_.begin());
      distance_.erase(distance_.begin());
    }
  }
  if (!active_.empty()) {
    unsigned int b = BracketType(static_cast<unsigned char>(active_.back()));
    unsigned int d = std::min(static_cast<unsigned int>(active_.size() - 1), 3u);
    unsigned int dist = distance_.back();
    if (dist >= distance_limit_) dist = distance_limit_ - 1;
    context_ = 1 + (d * 4 + b) * distance_limit_ + dist;
  } else {
    context_ = 0;
  }
}

bool BracketContext::IsEqual(Context* c) {
  BracketContext* p = dynamic_cast<BracketContext*>(c);
  if (!p) return false;
  if (distance_limit_ == p->distance_limit_ && stack_limit_ == p->stack_limit_)
    return true;
  return false;
}
