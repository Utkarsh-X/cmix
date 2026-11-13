#include "citation-context.h"
#include <cstring>

CitationContext::CitationContext(const unsigned int& bit_context)
    : byte_(bit_context) {
  context_ = 0;
  size_ = 64;
  mode_ = 0;
  dist_ = 0;
  ref_depth_ = 0;
  curly_depth_ = 0;
  bracket_depth_ = 0;
  in_cite_ = false;
  return_mode_ = 0;
  prev_byte_ = 0;
  std::memset(h_, 0, sizeof(h_));
}

bool CitationContext::EndsWith(int len, const char* str) const {
  return std::memcmp(h_ + 16 - len, str, len) == 0;
}

bool CitationContext::EndsWithInsensitive(int len, const char* str) const {
  for (int i = 0; i < len; ++i) {
    unsigned char a = h_[16 - len + i];
    unsigned char b = static_cast<unsigned char>(str[i]);
    if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
    if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
    if (a != b) return false;
  }
  return true;
}

void CitationContext::Update() {
  unsigned char c = static_cast<unsigned char>(byte_);
  std::memmove(h_, h_ + 1, 15);
  h_[15] = c;

  bool state_changed = false;

  // Track brackets for wikilinks
  if (c == '[' && prev_byte_ == '[') {
    if (bracket_depth_ < 8) ++bracket_depth_;
  } else if (c == ']' && prev_byte_ == ']') {
    if (bracket_depth_ > 0) --bracket_depth_;
  } else if (c == '\n') {
    bracket_depth_ = 0;
  }

  // URL tracking (Modes 6, 7, 8)
  if (mode_ >= 6 && mode_ <= 8) {
    if (c == '\n' || c == ' ' || c == '\t' || c == '\r' ||
        c == ']' || c == '|' || c == '}' || c == '>' ||
        c == '"' || c == 39 || c == '<') {
      mode_ = return_mode_;
      state_changed = true;
    } else if (mode_ == 6) {
      if (EndsWith(3, "://")) {
        mode_ = 7;
        state_changed = true;
      }
    } else if (mode_ == 7) {
      if (c == '/' || c == '?') {
        mode_ = 8;
        state_changed = true;
      }
    }
  } else {
    if (EndsWith(7, "http://") || EndsWith(8, "https://")) {
      return_mode_ = mode_;
      mode_ = 7;
      state_changed = true;
    } else if (EndsWith(5, "http:") || EndsWith(6, "https:")) {
      return_mode_ = mode_;
      mode_ = 6;
      state_changed = true;
    }
  }

  // Ref tracking (<ref> / &lt;ref>)
  if (mode_ < 6 || mode_ > 8) {
    if (EndsWith(7, "&lt;ref") || EndsWith(4, "<ref")) {
      mode_ = 1;
      ref_depth_ = 1;
      state_changed = true;
    } else if (mode_ == 1) {
      if (EndsWith(4, "&gt;") || c == '>') {
        if (EndsWith(5, "/&gt;") || EndsWith(2, "/>") || h_[10] == '/') {
          ref_depth_ = 0;
          mode_ = 0;
        } else {
          mode_ = 2;
        }
        state_changed = true;
      }
    } else if (mode_ == 2) {
      if (EndsWith(12, "&lt;/ref&gt;") || EndsWith(6, "</ref>")) {
        ref_depth_ = 0;
        mode_ = 0;
        state_changed = true;
      }
    }
  }

  // Template cite tracking ({{cite ...)
  if (mode_ < 6 || mode_ > 8) {
    if (c == '{' && prev_byte_ == '{') {
      if (curly_depth_ < 16) ++curly_depth_;
    } else if (c == '}' && prev_byte_ == '}') {
      if (curly_depth_ > 0) {
        --curly_depth_;
        if (curly_depth_ == 0 && in_cite_) {
          in_cite_ = false;
          mode_ = (ref_depth_ > 0) ? 2 : 0;
          state_changed = true;
        }
      }
    }

    if (curly_depth_ > 0) {
      if (EndsWithInsensitive(6, "{{cite")) {
        in_cite_ = true;
        mode_ = 3;
        state_changed = true;
      } else if (in_cite_ && bracket_depth_ == 0) {
        if (c == '|') {
          mode_ = 4;
          state_changed = true;
        } else if (c == '=' && mode_ == 4) {
          mode_ = 5;
          state_changed = true;
        }
      }
    }
  }

  // Distance tracking
  if (state_changed) {
    dist_ = 0;
  } else {
    if (dist_ < 7) ++dist_;
  }

  prev_byte_ = c;

  // Discrete 64-state context index
  if (mode_ == 0) {
    context_ = 0;
  } else if (mode_ >= 1 && mode_ <= 7) {
    context_ = 1 + (mode_ - 1) * 8 + (dist_ & 7);
  } else if (mode_ == 8) {
    context_ = 57 + (dist_ > 6 ? 6 : dist_);
  } else {
    context_ = 0;
  }
}

bool CitationContext::IsEqual(Context* c) {
  CitationContext* p = dynamic_cast<CitationContext*>(c);
  return p != nullptr;
}
