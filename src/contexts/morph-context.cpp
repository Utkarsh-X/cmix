#include "morph-context.h"
#include <cctype>

namespace {

inline uint8_t ClassifyBoundary(uint8_t c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return 0; // In-word
  if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return 1;  // Whitespace
  if (c == '[' || c == ']' || c == '|') return 2;                // Wikilink
  if (c == '(' || c == ')') return 3;                            // Parenthesis
  if (c == '<' || c == '>' || c == '/') return 4;                // XML tag
  return 5;                                                      // Other punctuation
}

inline uint8_t AdvanceSuffix(uint8_t cur_state, char c) {
  switch (cur_state) {
    case 1:
      if (c == 'i') return 2;  // ing, ity, ize, ible
      if (c == 't') return 3;  // tion
      if (c == 's') return 4;  // s, sion
      if (c == 'm') return 5;  // ment
      if (c == 'a') return 6;  // able, ation, al, ance
      if (c == 'n') return 7;  // ness
      if (c == 'e') return 8;  // ed, es, er, est, ence
      if (c == 'l') return 9;  // ly, less
      return 1;
    // -ing path
    case 2:
      if (c == 'n') return 10;
      if (c == 't') return 11; // -ity
      if (c == 'z') return 12; // -ize
      if (c == 'b') return 13; // -ible
      return 1;
    case 10:
      if (c == 'g') return 14; // terminal: -ing
      return 1;
    // -tion path
    case 3:
      if (c == 'i') return 15;
      return 1;
    case 15:
      if (c == 'o') return 16;
      return 1;
    case 16:
      if (c == 'n') return 17; // terminal: -tion
      return 1;
    // -ment path
    case 5:
      if (c == 'e') return 18;
      return 1;
    case 18:
      if (c == 'n') return 19;
      return 1;
    case 19:
      if (c == 't') return 20; // terminal: -ment
      return 1;
    // -ed / -es / -er / -est path
    case 8:
      if (c == 'd') return 21; // terminal: -ed
      if (c == 's') return 22; // terminal: -es
      if (c == 'r') return 23; // terminal: -er
      return 1;
    case 22:
      if (c == 't') return 24; // terminal: -est
      return 1;
    // -ly path
    case 9:
      if (c == 'y') return 25; // terminal: -ly
      return 1;
    default:
      return 1;
  }
}
} // namespace

MorphContext::MorphContext(const unsigned int& bit_context)
    : byte_(bit_context) {
  // 6 (boundary) * 26 (suffix trie) * 2 (stem hit) = 312 dense states
  size_ = 6 * 26 * 2;
  context_ = 0;
}

void MorphContext::InsertStem(uint64_t hash, uint8_t len) {
  int existing_idx = -1;
  for (int i = 0; i < cache_size_; ++i) {
    if (stem_cache_[i] == hash) {
      existing_idx = i;
      break;
    }
  }
  int shift_to = (existing_idx != -1) ? existing_idx : (cache_size_ < 16 ? cache_size_++ : 15);
  for (int i = shift_to; i > 0; --i) {
    stem_cache_[i] = stem_cache_[i - 1];
    stem_len_cache_[i] = stem_len_cache_[i - 1];
  }
  stem_cache_[0] = hash;
  stem_len_cache_[0] = len;
}

void MorphContext::Update() {
  uint8_t c_raw = static_cast<uint8_t>(byte_);
  boundary_type_ = ClassifyBoundary(c_raw);

  if (boundary_type_ == 0) {
    char c = static_cast<char>(std::tolower(c_raw));
    if (word_len_ < 31) {
      word_buf_[word_len_++] = c;
    }
    current_prefix_hash_ = current_prefix_hash_ * 2654435761ULL + static_cast<uint8_t>(c);

    if (!stem_hit_ && word_len_ >= 3) {
      for (int i = 0; i < cache_size_; ++i) {
        if (stem_len_cache_[i] == word_len_ && stem_cache_[i] == current_prefix_hash_) {
          stem_hit_ = true;
          break;
        }
      }
    }

    if (word_len_ >= 3) {
      if (suffix_state_ == 0) suffix_state_ = 1;
      suffix_state_ = AdvanceSuffix(suffix_state_, c);
      best_suffix_node_ = suffix_state_;
    } else {
      suffix_state_ = 1;
      best_suffix_node_ = 0;
    }
  } else {
    if (word_len_ >= 3) {
      uint8_t stem_len = word_len_;
      if (suffix_state_ == 14 && stem_len > 3 + 3) stem_len -= 3;
      else if (suffix_state_ == 17 && stem_len > 4 + 3) stem_len -= 4;
      else if (suffix_state_ == 20 && stem_len > 4 + 3) stem_len -= 4;
      else if (suffix_state_ == 21 && stem_len > 2 + 3) stem_len -= 2;
      else if (suffix_state_ == 25 && stem_len > 2 + 3) stem_len -= 2;

      uint64_t stem_hash = 0;
      for (int i = 0; i < stem_len; ++i) {
        stem_hash = stem_hash * 2654435761ULL + static_cast<uint8_t>(word_buf_[i]);
      }
      InsertStem(stem_hash, stem_len);
    }

    word_len_ = 0;
    current_prefix_hash_ = 0;
    suffix_state_ = 0;
    best_suffix_node_ = 0;
    stem_hit_ = false;
  }

  uint8_t safe_suffix = (best_suffix_node_ < 26) ? best_suffix_node_ : 0;
  context_ = static_cast<unsigned long long>(boundary_type_) * 52 +
             (stem_hit_ ? 26 : 0) + safe_suffix;
}

bool MorphContext::IsEqual(Context* c) {
  return dynamic_cast<MorphContext*>(c) != nullptr;
}
