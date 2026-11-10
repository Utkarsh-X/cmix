#ifndef CHAR_RATE_H_
#define CHAR_RATE_H_

#include <vector>

namespace char_rate {

inline signed char g_det[256];
inline unsigned char g_skew[256];
inline int g_ready = 0;

inline void Init(const std::vector<bool>& vocab) {
  for (unsigned node = 1; node < 256; ++node) {
    int j = 0;
    while ((node >> (j + 1)) != 0) ++j;
    const unsigned prefix = node - (1u << j);
    const unsigned width = 1u << (8 - j);
    const unsigned bot = prefix * width;
    const unsigned mid = bot + width / 2;
    int nlo = 0, nhi = 0;
    for (unsigned x = bot; x < mid; ++x) {
      if (x < vocab.size() && vocab[x]) ++nlo;
    }
    for (unsigned x = mid; x < bot + width; ++x) {
      if (x < vocab.size() && vocab[x]) ++nhi;
    }
    g_det[node] = (nlo == 0 && nhi > 0) ? 1 : ((nhi == 0 && nlo > 0) ? -1 : 0);
    g_skew[node] = (nlo + nhi) ? (unsigned char)((16 * nhi + (nlo + nhi) / 2)
                                                 / (nlo + nhi)) : 8;
  }
  g_det[0] = 0;
  g_skew[0] = 8;
  g_ready = 1;
}

inline int Forced(unsigned int node) {
  return g_det[node & 255];
}

}  // namespace char_rate

#endif  // CHAR_RATE_H_
