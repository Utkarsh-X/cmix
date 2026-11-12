#include "template-context.h"

TemplateContext::TemplateContext(const unsigned int& bit_context)
    : byte_(bit_context) {
  context_ = 0;
  // 1 (outside) + 3 (title, key, val) * 16 (clamped distance) = 49 dense states
  size_ = 1 + 3 * 16;
}

void TemplateContext::Update() {
  unsigned char c = static_cast<unsigned char>(byte_);
  if (c == '{' && prev_byte_ == '{') {
    if (curly_depth_ < 16) ++curly_depth_;
    state_ = 1; // Template title
    dist_ = 0;
  } else if (c == '}' && prev_byte_ == '}') {
    if (curly_depth_ > 0) {
      --curly_depth_;
      if (curly_depth_ == 0) state_ = 0;
    }
    dist_ = 0;
  } else if (curly_depth_ > 0) {
    if (c == '|') {
      state_ = 2; // Parameter key
      dist_ = 0;
    } else if (c == '=' && state_ == 2) {
      state_ = 3; // Parameter value
      dist_ = 0;
    } else {
      if (dist_ < 15) ++dist_;
    }
  } else {
    state_ = 0;
    dist_ = 0;
  }
  prev_byte_ = c;

  if (state_ == 0) {
    context_ = 0;
  } else {
    context_ = 1 + (state_ - 1) * 16 + dist_;
  }
}

bool TemplateContext::IsEqual(Context* c) {
  TemplateContext* p = dynamic_cast<TemplateContext*>(c);
  return p != nullptr;
}
