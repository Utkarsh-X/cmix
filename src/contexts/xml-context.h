#ifndef XML_CONTEXT_H
#define XML_CONTEXT_H

#include "context.h"
#include <cstdint>

class XmlContext : public Context {
 public:
  enum State : uint8_t {
    STATE_OUTSIDE = 0,
    STATE_TAG_OPEN = 1,
    STATE_TAG_CLOSE_SLASH = 2,
    STATE_OPEN_TAG_NAME_BASE = 3,    // 3..10 (8 states, dist 0..7)
    STATE_CLOSE_TAG_NAME_BASE = 11,  // 11..18 (8 states, dist 0..7)
    STATE_INSIDE_TAG_WS = 19,        // Whitespace/punct
    STATE_ATTR_KEY_BASE = 20,        // 20..23 (4 states, dist 0..3)
    STATE_ATTR_VAL_BASE = 24,        // 24..31 (8 states, dist 0..7)
    STATE_ENTITY_REF_BASE = 32,      // 32..37 (6 states, dist 0..5)
    STATE_COMMENT = 38,              // 38 (1 state)
    NUM_STATES = 39
  };

  explicit XmlContext(const unsigned int& bit_context);
  void Update() override;
  bool IsEqual(Context* c) override;

 private:
  const unsigned int& byte_;
  uint8_t fsm_mode_ = 0;
  int fsm_dist_ = 0;
  uint8_t quote_char_ = 0;
  uint8_t prev_byte_ = 0;
};

#endif  // XML_CONTEXT_H
