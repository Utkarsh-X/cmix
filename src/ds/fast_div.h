#ifndef FAST_DIV_H_
#define FAST_DIV_H_

#include <stdint.h>

namespace fastdiv {

// Bit-exact replacement for `n % d` with runtime-constant divisor d.
// Call Init(d) once at construction, Mod(n) / Div(n) on the hot path.
struct Div64 {
  typedef unsigned long long u64;

  u64 d_ = 1;
  u64 M_ = 0;
  u64 mask_ = 0;
  int s_ = 0;
  int p2s_ = 0;
  int a_ = 0;
  bool pot2_ = true;

  void Init(u64 d) {
    d_ = d;
    if (d <= 1 || (d & (d - 1)) == 0) {
      pot2_ = true;
      mask_ = (d == 0) ? 0 : (d - 1);
      p2s_ = (d <= 1) ? 0 : __builtin_ctzll(d);
      return;
    }
    pot2_ = false;
    const u64 kTwo63 = 0x8000000000000000ULL;
    const u64 kMax   = 0xFFFFFFFFFFFFFFFFULL;
    u64 nc = kMax - (kMax - d + 1) % d;
    int p = 63;
    u64 q1 = kTwo63 / nc,        r1 = kTwo63 - q1 * nc;
    u64 q2 = (kTwo63 - 1) / d,   r2 = (kTwo63 - 1) - q2 * d;
    u64 delta;
    a_ = 0;
    do {
      ++p;
      if (r1 >= nc - r1) { q1 = 2 * q1 + 1; r1 = 2 * r1 - nc; }
      else               { q1 = 2 * q1;     r1 = 2 * r1;      }
      if (r2 + 1 >= d - r2) {
        if (q2 >= kTwo63 - 1) a_ = 1;
        q2 = 2 * q2 + 1; r2 = 2 * r2 + 1 - d;
      } else {
        if (q2 >= kTwo63) a_ = 1;
        q2 = 2 * q2;     r2 = 2 * r2 + 1;
      }
      delta = d - 1 - r2;
    } while (p < 128 && (q1 < delta || (q1 == delta && r1 == 0)));
    M_ = q2 + 1;
    s_ = p - 64;
  }

  static inline u64 MulHi(u64 a, u64 b) {
#if defined(_MSC_VER) && defined(_M_X64)
    return __umulh(a, b);
#else
    return (u64)(((unsigned __int128)a * (unsigned __int128)b) >> 64);
#endif
  }

  inline u64 Div(u64 n) const {
    if (pot2_) return n >> p2s_;
    u64 q = MulHi(n, M_);
    if (a_) {
      q = ((n - q) >> 1) + q;
      return q >> (s_ - 1);
    }
    return q >> s_;
  }

  inline u64 Mod(u64 n) const {
    if (pot2_) return n & mask_;
    return n - Div(n) * d_;
  }
};

}  // namespace fastdiv

#endif  // FAST_DIV_H_
