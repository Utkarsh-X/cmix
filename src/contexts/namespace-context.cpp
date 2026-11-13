#include "namespace-context.h"
#include <cstring>

NamespaceContext::NamespaceContext(const unsigned int& bit_context)
    : byte_(bit_context) {
  context_ = 0;
  size_ = 64;
  mode_ = 0;
  dist_ = 0;
  bracket_depth_ = 0;
  prev_byte_ = 0;
  prefix_step_ = 0;
  cand_lang_len_ = 0;
  in_iw_block_ = false;
  std::memset(h_, 0, sizeof(h_));
}

bool NamespaceContext::EndsWith(int len, const char* str) const {
  if (len > 16) return false;
  return std::memcmp(h_ + 16 - len, str, len) == 0;
}

bool NamespaceContext::EndsWithInsensitive(int len, const char* str) const {
  if (len > 16) return false;
  for (int i = 0; i < len; ++i) {
    unsigned char a = h_[16 - len + i];
    unsigned char b = static_cast<unsigned char>(str[i]);
    if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
    if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
    if (a != b) return false;
  }
  return true;
}

void NamespaceContext::Update() {
  unsigned char c = static_cast<unsigned char>(byte_);
  std::memmove(h_, h_ + 1, 15);
  h_[15] = c;

  bool state_changed = false;

  // 1. Bracket tracking
  if (c == '[' && prev_byte_ == '[') {
    if (bracket_depth_ < 8) ++bracket_depth_;
    if (mode_ == 0 || mode_ == 7) {
      prefix_step_ = 0;
      cand_lang_len_ = 0;
    }
  } else if (c == ']' && prev_byte_ == ']') {
    if (bracket_depth_ > 0) --bracket_depth_;
  } else if (c == '\n') {
    bracket_depth_ = 0;
  }

  // 2. State transitions
  if (mode_ == 0 || mode_ == 7) {
    if (bracket_depth_ == 1 && c != '[') {
      if (c == 'C' || c == 'c') {
        mode_ = 1; // Category Prefix
        prefix_step_ = 1;
        state_changed = true;
      } else if (c == 'I' || c == 'i' || c == 'F' || c == 'f' || c == 'M' || c == 'm') {
        mode_ = 8; // Media Prefix
        prefix_step_ = 1;
        state_changed = true;
      } else if (c >= 'a' && c <= 'z') {
        mode_ = 4; // Interwiki Lang Code
        cand_lang_len_ = 1;
        state_changed = true;
      }
    } else if (mode_ == 7) {
      if (c == '\n') {
        state_changed = true;
      } else if (prev_byte_ == '\n' && c != '[') {
        mode_ = 0;
        in_iw_block_ = false;
        state_changed = true;
      }
    }
  } else if (mode_ == 1) { // Category Prefix matching 'ategory:'
    const char* target = "ategory:";
    if (prefix_step_ >= 1 && prefix_step_ <= 8) {
      unsigned char expected = static_cast<unsigned char>(target[prefix_step_ - 1]);
      unsigned char lower_c = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
      if (lower_c == expected) {
        ++prefix_step_;
        if (prefix_step_ > 8) {
          mode_ = 2; // Category Title starts
          state_changed = true;
        }
      } else {
        mode_ = 0;
        state_changed = true;
      }
    } else {
      mode_ = 0;
      state_changed = true;
    }
  } else if (mode_ == 2) { // Category Title
    if (c == '|') {
      mode_ = 3; // Category Sort Key
      state_changed = true;
    } else if (bracket_depth_ == 0 && c == ']' && prev_byte_ == ']') {
      mode_ = 0;
      state_changed = true;
    } else if (c == '\n') {
      mode_ = 0;
      state_changed = true;
    }
  } else if (mode_ == 3) { // Category Sort Key
    if (bracket_depth_ == 0 && c == ']' && prev_byte_ == ']') {
      mode_ = 0;
      state_changed = true;
    } else if (c == '\n') {
      mode_ = 0;
      state_changed = true;
    }
  } else if (mode_ == 4) { // Interwiki Lang Code
    if (c == ':') {
      if (cand_lang_len_ >= 2 && cand_lang_len_ <= 10) {
        mode_ = 5; // Interwiki Colon
        state_changed = true;
      } else {
        mode_ = 0;
        state_changed = true;
      }
    } else if ((c >= 'a' && c <= 'z') || c == '-') {
      ++cand_lang_len_;
      bool allow_longer = false;
      if (cand_lang_len_ <= 6) {
        if (EndsWith(cand_lang_len_, "simple")) allow_longer = true;
        if (EndsWith(cand_lang_len_, "zh-min")) allow_longer = true;
      }
      if (cand_lang_len_ > 3 && !allow_longer) {
        mode_ = 0;
        state_changed = true;
      }
    } else {
      mode_ = 0;
      state_changed = true;
    }
  } else if (mode_ == 5) { // Interwiki Colon
    mode_ = 6; // Interwiki Title starts
    state_changed = true;
  } else if (mode_ == 6) { // Interwiki Title
    if (bracket_depth_ == 0 && c == ']' && prev_byte_ == ']') {
      mode_ = 7; // Interwiki Delimiter / Block
      in_iw_block_ = true;
      state_changed = true;
    } else if (c == '\n') {
      mode_ = 0;
      state_changed = true;
    }
  } else if (mode_ == 8) { // Media Prefix: Image: / File: / Media:
    if (c == ':') {
      if (EndsWithInsensitive(6, "image:") ||
          EndsWithInsensitive(5, "file:") ||
          EndsWithInsensitive(6, "media:")) {
        mode_ = 9; // Media Target / Filename
        state_changed = true;
      } else {
        mode_ = 0;
        state_changed = true;
      }
    } else if (prefix_step_ < 7) {
      ++prefix_step_;
    } else {
      mode_ = 0;
      state_changed = true;
    }
  } else if (mode_ == 9) { // Media Target / Filename
    if (bracket_depth_ == 0 && c == ']' && prev_byte_ == ']') {
      mode_ = 0;
      state_changed = true;
    } else if (c == '|') {
      mode_ = 10; // Media Format Attribute
      state_changed = true;
    } else if (c == '\n') {
      mode_ = 0;
      state_changed = true;
    }
  } else if (mode_ == 10 || mode_ == 11) { // Media Attribute or Caption
    if (bracket_depth_ == 0 && c == ']' && prev_byte_ == ']') {
      mode_ = 0;
      state_changed = true;
    } else if (bracket_depth_ == 1 && c == '|') {
      mode_ = 10;
      state_changed = true;
    } else if (c == '\n') {
      mode_ = 0;
      state_changed = true;
    } else if (mode_ == 10) {
      if (c == ' ' || dist_ > 12 || bracket_depth_ > 1) {
        mode_ = 11; // Media Caption
        state_changed = true;
      }
    }
  }

  if (state_changed) {
    dist_ = 0;
  } else {
    if (dist_ < 7) ++dist_;
  }

  prev_byte_ = c;

  // Dense 64-state mapping strictly bounded
  if (mode_ == 0) {
    context_ = 0;
  } else if (mode_ == 1) {
    context_ = 1 + (dist_ > 7 ? 7 : dist_);
  } else if (mode_ == 2) {
    context_ = 9 + (dist_ > 7 ? 7 : dist_);
  } else if (mode_ == 3) {
    context_ = 17 + (dist_ > 5 ? 5 : dist_);
  } else if (mode_ == 4) {
    context_ = 23 + (dist_ > 3 ? 3 : dist_);
  } else if (mode_ == 5) {
    context_ = 27;
  } else if (mode_ == 6) {
    context_ = 28 + (dist_ > 7 ? 7 : dist_);
  } else if (mode_ == 7) {
    context_ = 36 + (dist_ > 1 ? 1 : dist_);
  } else if (mode_ == 8) {
    context_ = 38 + (dist_ > 5 ? 5 : dist_);
  } else if (mode_ == 9) {
    context_ = 44 + (dist_ > 7 ? 7 : dist_);
  } else if (mode_ == 10) {
    context_ = 52 + (dist_ > 5 ? 5 : dist_);
  } else if (mode_ == 11) {
    context_ = 58 + (dist_ > 5 ? 5 : dist_);
  } else {
    context_ = 0;
  }
}

bool NamespaceContext::IsEqual(Context* c) {
  NamespaceContext* p = dynamic_cast<NamespaceContext*>(c);
  return p != nullptr;
}
