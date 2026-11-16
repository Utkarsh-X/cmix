#include "context-manager.h"
#include "contexts/page_contexts.h"

extern const unsigned char wrt_2b[256];
extern const unsigned char wrt_3b[256];

ContextManager::ContextManager() : history_(100000000, 0),
    shared_map_(256*8000000, 0), words_(8, 0), recent_bytes_(8, 0) {}

const Context& ContextManager::AddContext(std::unique_ptr<Context> context) {
  for (const auto& old : contexts_) {
    if (old->IsEqual(context.get())) return *old;
  }
  contexts_.push_back(std::move(context));
  return *(contexts_[contexts_.size() - 1]);
}

const BitContext& ContextManager::AddBitContext(std::unique_ptr<BitContext>
    bit_context) {
  for (const auto& old : bit_contexts_) {
    if (old->IsEqual(bit_context.get())) return *old;
  }
  bit_contexts_.push_back(std::move(bit_context));
  return *(bit_contexts_[bit_contexts_.size() - 1]);
}

void ContextManager::UpdateHistory() {
  history_[history_pos_] = bit_context_;
  ++history_pos_;
  if (history_pos_ == history_.size()) history_pos_ = 0;
}

void ContextManager::UpdateWords() {
  unsigned char c = bit_context_;

  // EXP-177: Layer-0 syntax stream & word boundary tracking from ex-girlfriend
  words = words * 2;
  if ((c >= 'a' && c <= 'z') || c >= 0x80) words += 1;
  if (c == '\n') words = 0xfffc;
  else if (c == '.' || c == ',') words |= 0xffc;

  n2bState = wrt_2b[c];
  b2stream = b2stream * 4 + n2bState;
  b2streamcxt = b2stream & 0x3ff;

  if (c == '}' || c == '{' || c == ']') b3stream = (b3stream & 0xfffffff8) + 3;
  else if (c == '=') b3stream = (b3stream & 0xfffffff8) + 4;
  n3bState = wrt_3b[c];
  b3stream = b3stream * 8 + n3bState;
  if (c == 10 || c == ')') b3stream = b3stream << 6;
  if (c == '|') b3stream = b3stream * 8 + wrt_3b[c];
  b3streamcxt = b3stream & 0x1ff;

  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80) {
    words_[7] = words_[7] * 997*16 + c;
  } else {
    words_[7] = 0;
  }
  if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == 8 || c == 6 ||
      c >= 0x80) {
    words_[0] = words_[0] * 997*16 + c;
    words_[0] &= 0xfffffff;
    words_[1] = words_[1] * 263*32 + c;
  } else {
    for (int i = 6; i >= 2; --i) {
      words_[i] = words_[i-1];
    }
    words_[1] = 0;
  }
}

void ContextManager::UpdateRecentBytes() {
  for (int i = 7; i >= 1; --i) {
    recent_bytes_[i] = recent_bytes_[i-1];
  }
  recent_bytes_[0] = bit_context_;
}

void ContextManager::UpdateWRTContext() {
  if (bit_context_ < 0x80) {
    wrt_state_ = 0;
  } else {
    if (wrt_state_ == 0) wrt_context_ = 0;
    wrt_state_ = 1;
    wrt_context_ <<= 8;
    wrt_context_ += bit_context_;
    if (wrt_context_ > 0xFFEFCF) wrt_context_ = 0;
  }
}

void ContextManager::UpdatePPMDOrderRegime(int ppmd_order) {
  // EXP-183: 4-State Empirical Entropy Plateau Partition
  if (ppmd_order >= 12) ppmd_order_regime_ = 3;
  else if (ppmd_order >= 5) ppmd_order_regime_ = 2;
  else if (ppmd_order >= 2) ppmd_order_regime_ = 1;
  else ppmd_order_regime_ = 0;
}

void ContextManager::UpdateContexts(int bit) {
  bit_context_ += bit_context_ + bit;
  long_bit_context_ = bit_context_;
  if (bit_context_ >= 256) {
    bit_context_ -= 256;
    long_bit_context_ = 1;
    longest_match_ = 0;

    if (bit_context_ == '\n') {
      line_break_ = 0;
    } else if (line_break_ < 99) {
      ++line_break_;
    }

    UpdateHistory();
    UpdateWords();
    UpdateRecentBytes();
    UpdateWRTContext();
    for (const auto& context : contexts_) {
      context->Update();
    }
  }
  for (const auto& context : bit_contexts_) {
    context->Update();
  }
  // EXP-177: Update bit-conditioned contexts on every bit
  wordscxt = (words & 0x7F) * 256 + long_bit_context_;
  page_sur = (unsigned long long)pagectx::surprise.cxtmix * 256 + long_bit_context_;
}
