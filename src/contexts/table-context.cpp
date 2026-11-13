#include "table-context.h"

TableContext::TableContext(const unsigned int& bit_context)
    : byte_(bit_context) {
  context_ = 0;
  size_ = 64; // Strictly bounded 64 discrete states (0..63)
}

void TableContext::Update() {
  unsigned char c = static_cast<unsigned char>(byte_);

  // Track wikilink brackets [[ ... ]] so internal pipes (|) in links do not trigger cell boundaries
  if (c == '[' && prev_byte_ == '[') {
    if (bracket_depth_ < 8) ++bracket_depth_;
  } else if (c == ']' && prev_byte_ == ']') {
    if (bracket_depth_ > 0) --bracket_depth_;
  }

  bool state_changed = false;

  // Causal transitions strictly based on past decoded bytes:
  // Table start: opening '{|' at line start (line_pos_ == 1, prev_byte_ == '{', c == '|')
  if (line_pos_ == 1 && prev_byte_ == '{' && c == '|') {
    if (table_depth_ < 8) ++table_depth_;
    mode_ = 1; // Table header / attributes
    state_changed = true;
  } else if (table_depth_ > 0) {
    if (line_pos_ == 0 && c == '!') {
      mode_ = 3; // Header cell
      state_changed = true;
    } else if (line_pos_ == 1 && prev_byte_ == '|') {
      if (c == '-') {
        mode_ = 2; // Row separator / attributes
        state_changed = true;
      } else if (c == '+') {
        mode_ = 1; // Caption treated with table attributes
        state_changed = true;
      } else if (c == '}') {
        if (table_depth_ > 0) --table_depth_;
        mode_ = 8; // Table close
        state_changed = true;
      } else if (c != '|') {
        mode_ = 5; // Cell text
        state_changed = true;
      }
    } else if (line_pos_ > 1 && bracket_depth_ == 0) {
      // Inline cell / header delimiters
      if (c == '!' && prev_byte_ == '!') {
        mode_ = 3; // Inline header !!
        state_changed = true;
      } else if (c == '|' && prev_byte_ == '|') {
        mode_ = 5; // Inline cell ||
        state_changed = true;
      } else if (c == '|' && prev_byte_ != '|') {
        // Attribute-to-content separator: attr | content
        if (mode_ == 7) {
          mode_ = 5;
          state_changed = true;
        } else if (mode_ == 4) {
          mode_ = 3;
          state_changed = true;
        }
      }
    }
  }

  // Active numeric sub-state: when in cell text or header cell, digits switch to numeric distribution
  int active_mode = mode_;
  if ((mode_ == 5 || mode_ == 3) && (c >= '0' && c <= '9')) {
    active_mode = 6; // Cell numeric
  }

  // Line tracking and reset on newline
  if (c == '\n') {
    line_pos_ = 0;
    bracket_depth_ = 0;
    if (mode_ == 8 || table_depth_ == 0) {
      mode_ = 0;
      state_changed = true;
    }
  } else {
    ++line_pos_;
  }

  // Clamped distance tracking (0..7)
  if (state_changed) {
    dist_ = 0;
  } else {
    if (dist_ < 7) ++dist_;
  }

  prev_byte_ = c;

  // Dense context mapping: strictly 0..63
  if (active_mode == 0) {
    context_ = 0;
  } else if (active_mode >= 1 && active_mode <= 7) {
    context_ = 1 + (active_mode - 1) * 8 + (dist_ & 7);
  } else if (active_mode == 8) {
    context_ = 57 + (dist_ > 6 ? 6 : dist_);
  } else {
    context_ = 0;
  }
}

bool TableContext::IsEqual(Context* c) {
  TableContext* p = dynamic_cast<TableContext*>(c);
  return p != nullptr;
}
