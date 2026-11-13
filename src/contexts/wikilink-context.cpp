#include "wikilink-context.h"

WikilinkContext::WikilinkContext(const unsigned int& bit_context)
    : byte_(bit_context) {
  context_ = 0;
  size_ = 64; // Strictly bounded <= 64 discrete states
}

void WikilinkContext::Update() {
  unsigned char c = static_cast<unsigned char>(byte_);

  // 1. Section Header detection: opening '==' preceded by newline or start-of-line
  if (line_start_ && c == '=' && prev_byte_ == '=') {
    state_ = 4; // Section Header
    dist_ = 0;
    line_start_ = false;
  } else if (state_ == 4) {
    if (c == '\n') {
      state_ = 0;
      dist_ = 0;
      line_start_ = true;
    } else if (c == '=' && prev_byte_ == '=') {
      // Closing ==
      state_ = 0;
      dist_ = 0;
    } else {
      if (dist_ < 14) ++dist_;
    }
  }
  // 2. Wikilinks [[ ... ]]
  else if (c == '[' && prev_byte_ == '[') {
    if (bracket_depth_ < 16) ++bracket_depth_;
    state_ = 1; // Wikilink Target
    dist_ = 0;
  } else if (c == ']' && prev_byte_ == ']') {
    if (bracket_depth_ > 0) {
      --bracket_depth_;
      if (bracket_depth_ == 0) state_ = 0;
    }
    dist_ = 0;
  } else if (c == '\n') {
    // Wikilinks cannot cross newlines in MediaWiki wikitext
    bracket_depth_ = 0;
    state_ = 0;
    dist_ = 0;
    line_start_ = true;
  } else if (bracket_depth_ > 0) {
    if (state_ == 1) {
      // Causal prefix match for "Category:" (dist_ 0..8) with 0 lookahead
      static const char kCat[] = "Category:";
      if (dist_ < 9 && c == static_cast<unsigned char>(kCat[dist_])) {
        if (dist_ == 8) {
          state_ = 3; // Promoted to Category!
          dist_ = 0;
        } else {
          ++dist_;
        }
      } else if (c == '|') {
        state_ = 2; // Piped Anchor
        dist_ = 0;
      } else {
        if (dist_ < 15) ++dist_;
      }
    } else if (state_ == 2) { // Anchor
      if (dist_ < 15) ++dist_;
    } else if (state_ == 3) { // Category
      if (c == '|') {
        dist_ = 0; // Category sortkey reset
      } else {
        if (dist_ < 15) ++dist_;
      }
    }
  } else {
    state_ = 0;
    dist_ = 0;
  }

  // Update line_start_
  if (c == '\n') {
    line_start_ = true;
  } else if (c != ' ' && c != '\t' && c != '=') {
    line_start_ = false;
  }

  prev_byte_ = c;

  // Compute dense discrete context index: 0..63
  if (state_ == 0) {
    context_ = 0;
  } else if (state_ == 1) {
    context_ = 1 + dist_;       // 1..16
  } else if (state_ == 2) {
    context_ = 17 + dist_;      // 17..32
  } else if (state_ == 3) {
    context_ = 33 + dist_;      // 33..48
  } else if (state_ == 4) {
    context_ = 49 + dist_;      // 49..63
  }
}

bool WikilinkContext::IsEqual(Context* c) {
  WikilinkContext* p = dynamic_cast<WikilinkContext*>(c);
  return p != nullptr;
}
