#ifndef TEMPLATE_CONTEXT_H
#define TEMPLATE_CONTEXT_H

#include "context.h"

class TemplateContext : public Context {
 public:
  explicit TemplateContext(const unsigned int& bit_context);
  void Update() override;
  bool IsEqual(Context* c) override;

 private:
  const unsigned int& byte_;
  unsigned char prev_byte_ = 0;
  int curly_depth_ = 0;
  int state_ = 0; // 0: outside, 1: title, 2: param key, 3: param val
  int dist_ = 0;  // 0..15 clamped distance from boundary
};

#endif
