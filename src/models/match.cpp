#include "match.h"

Match::Match(const std::vector<unsigned char>& history,
    const unsigned long long& byte_context, const unsigned int& bit_context,
    int limit, float delta, unsigned long long map_size,
    unsigned long long* longest_match) : history_(history),
    byte_context_(byte_context), bit_context_(bit_context), history_pos_(0),
    cur_match_(0), cur_byte_(0), bit_pos_(128), match_length_(0),
    longest_match_(longest_match), limit_(limit),delta_(delta),
    divisor_(1.0 / (limit + delta)), map_(map_size, 0), tags_(map_size, 0) {
  div_map_.Init(map_.size());
  div_hist_.Init(history_.size());
  for (int i = 0; i < 256; ++i) {
    predictions_[i] = 0.5 + (i + 0.5) / 512;
  }
  counts_.fill(0);
}

const std::valarray<float>& Match::Predict() {
  if (cur_match_ == 0) {
    outputs_[0] = 0.5f;
    return outputs_;
  }
  if (cur_byte_ & bit_pos_) outputs_[0] = predictions_[match_length_];
  else outputs_[0] = 1 - predictions_[match_length_];
  return outputs_;
}

void Match::Perceive(int bit) {
  if (cur_match_ != 0) {
    int match = (bit == ((cur_byte_ & bit_pos_) != 0)) ? 1 : 0;
    float divisor = divisor_;
    if (counts_[match_length_] < limit_) {
      ++counts_[match_length_];
      divisor = 1.0 / (counts_[match_length_] + delta_);
    }
    predictions_[match_length_] +=
        (match - predictions_[match_length_]) * divisor;

    if (match) {
      if (match_length_ < 255) ++match_length_;
    } else {
      match_length_ = 0;
      cur_match_ = 0;
    }
  } else {
    match_length_ = 0;
  }
  bit_pos_ /= 2;

  if (bit_context_ >= 128) {
    uint32_t slot = div_map_.Mod(byte_context_);
    map_[slot] = history_pos_;
    hist_tag_ = (unsigned short)((hist_tag_ * 33) ^ bit_context_) | 1;
    tags_[slot] = hist_tag_;
    ++history_pos_;
  }
}

void Match::ByteUpdate() {
  if (match_length_ < 8) {
    uint32_t slot = div_map_.Mod(byte_context_);
    unsigned short tag = hist_tag_;
    if (tags_[slot] == tag) {
      cur_match_ = map_[slot];
    } else {
      cur_match_ = 0;
    }
  } else {
    ++cur_match_;
  }
  cur_match_ = div_hist_.Mod(cur_match_);
  cur_byte_ = history_[cur_match_];
  bit_pos_ = 128;

  unsigned long long match_context = match_length_ / 32;
  *longest_match_ = std::max(*longest_match_, match_context);
}
