#include "xml-context.h"
#include <algorithm>

XmlContext::XmlContext(const unsigned int& bit_context) : byte_(bit_context) {
  context_ = STATE_OUTSIDE;
  size_ = NUM_STATES;
}

void XmlContext::Update() {
  uint8_t b = static_cast<uint8_t>(byte_);

  switch (fsm_mode_) {
    case 0: // OUTSIDE
      if (b == '<') { fsm_mode_ = 1; fsm_dist_ = 0; }
      else if (b == '&') { fsm_mode_ = 7; fsm_dist_ = 0; }
      break;

    case 1: // Just saw '<'
      if (b == '/') { fsm_mode_ = 3; fsm_dist_ = 0; }
      else if (b == '!') { fsm_mode_ = 8; fsm_dist_ = 0; }
      else if (b == '>') { fsm_mode_ = 0; fsm_dist_ = 0; }
      else if (b == ' ' || b == '\t' || b == '\r' || b == '\n') { fsm_mode_ = 4; fsm_dist_ = 0; }
      else { fsm_mode_ = 2; fsm_dist_ = 0; }
      break;

    case 2: // OPEN_TAG_NAME
      if (b == '>') { fsm_mode_ = 0; fsm_dist_ = 0; }
      else if (b == '/' || b == ' ' || b == '\t' || b == '\r' || b == '\n') { fsm_mode_ = 4; fsm_dist_ = 0; }
      else { ++fsm_dist_; }
      break;

    case 3: // CLOSE_TAG_NAME
      if (b == '>') { fsm_mode_ = 0; fsm_dist_ = 0; }
      else if (b == ' ' || b == '\t' || b == '\r' || b == '\n') { fsm_mode_ = 4; fsm_dist_ = 0; }
      else { ++fsm_dist_; }
      break;

    case 4: // IN_TAG_WS / punctuation
      if (b == '>') { fsm_mode_ = 0; fsm_dist_ = 0; }
      else if (b == '"' || b == '\'') { fsm_mode_ = 6; quote_char_ = b; fsm_dist_ = 0; }
      else if (b == ' ' || b == '\t' || b == '\r' || b == '\n' || b == '/' || b == '=') {}
      else { fsm_mode_ = 5; fsm_dist_ = 0; }
      break;

    case 5: // ATTR_KEY
      if (b == '>') { fsm_mode_ = 0; fsm_dist_ = 0; }
      else if (b == '=' || b == ' ' || b == '\t' || b == '\r' || b == '\n') { fsm_mode_ = 4; fsm_dist_ = 0; }
      else { ++fsm_dist_; }
      break;

    case 6: // ATTR_VAL
      if (b == quote_char_) { fsm_mode_ = 4; fsm_dist_ = 0; }
      else { ++fsm_dist_; }
      break;

    case 7: // ENTITY
      if (b == ';' || b == ' ' || b == '<' || fsm_dist_ >= 8) { fsm_mode_ = 0; fsm_dist_ = 0; }
      else { ++fsm_dist_; }
      break;

    case 8: // COMMENT
      if (b == '>' && prev_byte_ == '-') { fsm_mode_ = 0; fsm_dist_ = 0; }
      break;
  }

  prev_byte_ = b;

  switch (fsm_mode_) {
    case 0: context_ = STATE_OUTSIDE; break;
    case 1: context_ = STATE_TAG_OPEN; break;
    case 2: context_ = STATE_OPEN_TAG_NAME_BASE + std::min(fsm_dist_, 7); break;
    case 3: context_ = STATE_CLOSE_TAG_NAME_BASE + std::min(fsm_dist_, 7); break;
    case 4: context_ = STATE_INSIDE_TAG_WS; break;
    case 5: context_ = STATE_ATTR_KEY_BASE + std::min(fsm_dist_, 3); break;
    case 6: context_ = STATE_ATTR_VAL_BASE + std::min(fsm_dist_, 7); break;
    case 7: context_ = STATE_ENTITY_REF_BASE + std::min(fsm_dist_, 5); break;
    case 8: context_ = STATE_COMMENT; break;
    default: context_ = STATE_OUTSIDE; break;
  }
}

bool XmlContext::IsEqual(Context* c) {
  return dynamic_cast<XmlContext*>(c) != nullptr;
}
