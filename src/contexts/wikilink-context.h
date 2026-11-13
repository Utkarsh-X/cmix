#ifndef WIKILINK_CONTEXT_H
#define WIKILINK_CONTEXT_H

#include "context.h"

// Wikilink & Section Header FSM:
// State 0: Outside (1 state)
// Mode 1: Wikilink Target [[Target...  (clamped dist 0..15 -> 16 states: 1..16)
// Mode 2: Wikilink Anchor |Anchor...   (clamped dist 0..15 -> 16 states: 17..32)
// Mode 3: Category [[Category:...      (clamped dist 0..15 -> 16 states: 33..48)
// Mode 4: Section Header == Title...   (clamped dist 0..14 -> 15 states: 49..63)
// Total dense context size: 1 + 16 + 16 + 16 + 15 = 64 states.
class WikilinkContext : public Context {
 public:
  explicit WikilinkContext(const unsigned int& bit_context);
  void Update() override;
  bool IsEqual(Context* c) override;

 private:
  const unsigned int& byte_;
  unsigned char prev_byte_ = 0;
  int bracket_depth_ = 0;
  int state_ = 0;        // 0: outside, 1: target, 2: anchor, 3: category, 4: header
  int dist_ = 0;         // clamped distance
  bool line_start_ = true;
};

#endif
