#ifndef SSE_H
#define SSE_H

#include <cstdint>

namespace SSE_sh {
  struct M_T1;
}

class SSE {
 public:
  SSE();
  ~SSE();
  float Predict(float input, uint8_t ctx_s1 = 0, uint8_t ctx_s2 = 0);
  void Perceive(int bit);

 private:
  SSE_sh::M_T1* sse_;
};

#endif
