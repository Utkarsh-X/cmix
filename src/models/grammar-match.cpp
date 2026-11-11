#include "grammar-match.h"

GrammarMatch::GrammarMatch(const unsigned int& bit_context, int limit,
    float delta) : Model(1), bit_context_(bit_context), limit_(limit), delta_(delta),
    divisor_(1.0f / (limit + delta)) {
  predictions_.fill(0.5f);
  counts_.fill(0);
  tbuf_.fill(0);
  title_.fill(0);
  fail_.fill(0);
  tgt_.fill(0);
  outputs_[0] = 0.5f;
}

GrammarMatch::~GrammarMatch() {}

const std::valarray<float>& GrammarMatch::Predict() {
  if (!have_expectation_) {
    outputs_[0] = 0.5f;
    return outputs_;
  }
  if (expected_byte_ & bit_pos_) {
    outputs_[0] = predictions_[state_];
  } else {
    outputs_[0] = 1.0f - predictions_[state_];
  }
  return outputs_;
}

void GrammarMatch::Perceive(int bit) {
  if (!have_expectation_) return;
  int match = (bit == ((expected_byte_ & bit_pos_) != 0)) ? 1 : 0;
  bit_pos_ >>= 1;

  float divisor = divisor_;
  if (counts_[state_] < limit_) {
    ++counts_[state_];
    divisor = 1.0f / (counts_[state_] + delta_);
  }
  predictions_[state_] += (match - predictions_[state_]) * divisor;

  if (!match) {
    have_expectation_ = false;
  }
}

void GrammarMatch::ByteUpdate() {
  ParseByte(static_cast<unsigned char>(bit_context_));
  bit_pos_ = 128;
}

void GrammarMatch::ParseByte(unsigned char c) {
  recent_ = (recent_ << 8) | c;

  // Title capture: in preprocessed Wikipedia XML, <title> is encoded as
  // 0x3C, 0xDF, 0x9B, 0x3E (4 bytes). The title ends at '<' (of </title>).
  if (!in_title_) {
    if ((recent_ & 0xFFFFFFFFULL) == 0x3CDF9B3EULL) {
      in_title_ = true;
      tbuf_len_ = 0;
      title_len_ = 0;
      echo_k_ = 0;
    }
  } else {
    if (c == '<') {
      FinishTitle();
    } else if (tbuf_len_ >= kTitleCap) {
      in_title_ = false;
      tbuf_len_ = 0;
    } else {
      tbuf_[tbuf_len_++] = c;
    }
  }

  BracketAdvance(c);
  if (!in_title_ && title_len_ > 0) EchoAdvance(c);
  ExportExpectation();
}

void GrammarMatch::BracketAdvance(unsigned char c) {
  if (c == '[') {
    br_state_ = kBrTarget;
    tgt_len_ = 0;
    return;
  }
  switch (br_state_) {
    case kBrIdle:
      break;
    case kBrTarget:
      if (c == '|') {
        if (tgt_len_ > 0) {
          br_state_ = kBrLabel;
          label_j_ = (tgt_[0] == 0x40 && tgt_len_ > 1) ? 1 : 0;
          label_pos_ = 0;
        } else {
          br_state_ = kBrIdle;
        }
      } else if (c == ']' || AbortClass(c) || tgt_len_ >= kTargetCap) {
        br_state_ = kBrIdle;
      } else {
        tgt_[tgt_len_++] = c;
      }
      break;
    case kBrLabel:
      if (c == ']') {
        br_state_ = kBrIdle;
      } else if (c == '|' || AbortClass(c)) {
        br_state_ = kBrIdle;
      } else if (label_j_ < tgt_len_ && c == tgt_[label_j_]) {
        ++label_j_;
        if (label_j_ >= tgt_len_) br_state_ = kBrPostCopy;
      } else if (label_pos_ == 0 && label_j_ == 1 && c == 0x40) {
        // Label carries its own capitalization byte 0x40
      } else {
        br_state_ = kBrScan;
      }
      ++label_pos_;
      break;
    case kBrPostCopy:
      if (c == ']') {
        br_state_ = kBrIdle;
      } else if (AbortClass(c)) {
        br_state_ = kBrIdle;
      } else {
        br_state_ = kBrScan;
      }
      ++label_pos_;
      break;
    case kBrScan:
      if (c == ']' || AbortClass(c) || ++label_pos_ > kTargetCap) {
        br_state_ = kBrIdle;
      }
      break;
  }
}

void GrammarMatch::EchoAdvance(unsigned char c) {
  while (echo_k_ > 0 && c != title_[echo_k_]) echo_k_ = fail_[echo_k_ - 1];
  if (c == title_[echo_k_]) ++echo_k_;
  if (echo_k_ >= title_len_) echo_k_ = fail_[title_len_ - 1];
}

void GrammarMatch::FinishTitle() {
  in_title_ = false;
  title_len_ = tbuf_len_;
  echo_k_ = 0;
  if (title_len_ <= 0) return;
  for (int i = 0; i < title_len_; ++i) title_[i] = tbuf_[i];
  fail_[0] = 0;
  for (int i = 1; i < title_len_; ++i) {
    int k = fail_[i - 1];
    while (k > 0 && title_[i] != title_[k]) k = fail_[k - 1];
    if (title_[i] == title_[k]) ++k;
    fail_[i] = k;
  }
}

void GrammarMatch::ExportExpectation() {
  have_expectation_ = false;
  if (br_state_ == kBrLabel && label_j_ < tgt_len_) {
    expected_byte_ = tgt_[label_j_];
    state_ = kFamilyPiped * 32 + (label_j_ < 31 ? label_j_ : 31);
    have_expectation_ = true;
  } else if (br_state_ == kBrPostCopy) {
    expected_byte_ = ']';
    state_ = kFamilyPipedPostCopy * 32;
    have_expectation_ = true;
  } else if (!in_title_ && echo_k_ >= kEchoArm && echo_k_ < title_len_) {
    expected_byte_ = title_[echo_k_];
    state_ = kFamilyEcho * 32 + (echo_k_ < 31 ? echo_k_ : 31);
    have_expectation_ = true;
  }
}
