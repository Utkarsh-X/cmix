#ifndef B1_INTERP_H_
#define B1_INTERP_H_

#include <map>
#include <iterator>

namespace b1_interp {

// Floor division towards -infinity (Howard Hinnant / ts_algebra convention)
static inline long long Fdiv(long long a, long long b) {
  long long q = a / b, r = a % b;
  if (r != 0 && ((r < 0) != (b < 0))) --q;
  return q;
}

// E1 2D Linear Page-ID / Revision-ID Bounding Forecaster
// Predicts rev-id from page-id via linear interpolation between the
// already-decoded bounding B1 pages nearest in page-id (78% exact match on enwik9).
class Interp {
 public:
  long long Prognose(long long p) const {
    if (m_.empty()) return 15898943LL;
    auto hi = m_.lower_bound(p);
    const bool h2 = (hi != m_.end());
    const bool h1 = (hi != m_.begin());
    if (h1 && h2) {
      auto lo = std::prev(hi);
      const long long q1 = lo->first, r1 = lo->second;
      const long long q2 = hi->first, r2 = hi->second;
      if (q1 == q2) return r1;
      return r1 + Fdiv((p - q1) * (r2 - r1), q2 - q1);
    }
    if (h1) {
      auto lo = std::prev(hi);
      return lo->second + (p - lo->first);
    }
    if (h2) {
      return hi->second - (hi->first - p);
    }
    return 15898943LL;
  }

  void Add(long long p, long long r) {
    m_[p] = r;
  }

  void Clear() {
    m_.clear();
  }

  size_t Size() const {
    return m_.size();
  }

 private:
  std::map<long long, long long> m_;
};

}  // namespace b1_interp

#endif  // B1_INTERP_H_
