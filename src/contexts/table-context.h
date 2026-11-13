#ifndef TABLE_CONTEXT_H
#define TABLE_CONTEXT_H

#include "context.h"

// TableContext: Dense 64-state structural FSM tracking Wikipedia Wikitable syntax.
// Conditioned states:
// - State 0: Outside table (depth == 0)
// - Mode 1: Table header attributes {|... (dist 0..7 -> states 1..8)
// - Mode 2: Row separator attributes |-... (dist 0..7 -> states 9..16)
// - Mode 3: Header cell !... or !!... (dist 0..7 -> states 17..24)
// - Mode 4: Header attributes ! attr | (dist 0..7 -> states 25..32)
// - Mode 5: Cell text |... or ||... (dist 0..7 -> states 33..40)
// - Mode 6: Cell numeric [0-9]... (dist 0..7 -> states 41..48)
// - Mode 7: Cell attributes | attr | (dist 0..7 -> states 49..56)
// - Mode 8: Table close |}... (dist 0..6 -> states 57..63)
// Total Size: 64 discrete states (fits in 6-bit index 0..63).
class TableContext : public Context {
 public:
  explicit TableContext(const unsigned int& bit_context);
  void Update() override;
  bool IsEqual(Context* c) override;

 private:
  const unsigned int& byte_;
  unsigned char prev_byte_ = 0;
  int table_depth_ = 0;
  int mode_ = 0;
  int dist_ = 0;
  int line_pos_ = 0;
  int bracket_depth_ = 0;
};

#endif
