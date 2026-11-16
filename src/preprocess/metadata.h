#ifndef METADATA_H_
#define METADATA_H_

#include <stdio.h>
#include <deque>
#include <map>
#include <string>

namespace preprocessor {
class Dictionary;
}

namespace metadata {

enum TransformFlags {
  TRANSFORM_NONE = 0,
  TRANSFORM_PAGE_IDS = 1,
  TRANSFORM_TIMESTAMPS = 2,
  TRANSFORM_BOILERPLATE = 4,
  TRANSFORM_REV_IDS = 8,
  TRANSFORM_WIKILINKS = 16,
  TRANSFORM_ALL = (TRANSFORM_PAGE_IDS | TRANSFORM_TIMESTAMPS | TRANSFORM_BOILERPLATE | TRANSFORM_REV_IDS | TRANSFORM_WIKILINKS)
};

// Exact Howard Hinnant date arithmetic
static inline long long Fdiv(long long a, long long b) {
  long long q = a / b, r = a % b;
  if (r != 0 && ((r < 0) != (b < 0))) --q;
  return q;
}

static inline long long DaysFromCivil(long long y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097LL + static_cast<long long>(doe) - 719468LL;
}

static inline void CivilFromDays(long long z, long long* y, unsigned* m, unsigned* d) {
  z += 719468LL;
  const long long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long long yy = static_cast<long long>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp + (mp < 10 ? 3 : -9);
  *y = yy + (*m <= 2);
}

static inline long long TsToSeconds(long long y, unsigned m, unsigned d, unsigned hh, unsigned mm, unsigned ss) {
  return DaysFromCivil(y, m, d) * 86400LL + hh * 3600LL + mm * 60LL + ss;
}

static inline void SecondsToTs(long long sec, long long* y, unsigned* m, unsigned* d, unsigned* hh, unsigned* mm, unsigned* ss) {
  long long days = Fdiv(sec, 86400LL);
  long long rem = sec - days * 86400LL;
  *hh = static_cast<unsigned>(rem / 3600LL);
  rem %= 3600LL;
  *mm = static_cast<unsigned>(rem / 60LL);
  *ss = static_cast<unsigned>(rem % 60LL);
  CivilFromDays(days, y, m, d);
}

// E1: 2D Spatial Linear Interpolator
class B1Interp {
 public:
  static const long long kB1Max = 17000000LL;
  static const long long kLeer = 15898943LL;

  long long Prognose(long long p) const {
    if (m_.empty()) return kLeer;
    auto hi = m_.lower_bound(p);
    bool h2 = (hi != m_.end());
    bool h1 = (hi != m_.begin());
    if (h1 && h2) {
      auto lo = std::prev(hi);
      long long q1 = lo->first, r1 = lo->second;
      long long q2 = hi->first, r2 = hi->second;
      if (q2 == q1) return r1;
      return r1 + Fdiv((p - q1) * (r2 - r1), q2 - q1);
    }
    if (h1) {
      auto lo = std::prev(hi);
      return lo->second + (p - lo->first);
    }
    if (h2) return hi->second - (hi->first - p);
    return kLeer;
  }
  void Add(long long p, long long r) { m_[p] = r; }
  void Reset() { m_.clear(); }
 private:
  std::map<long long, long long> m_;
};

// E3b: 8-Interval Forecaster
class B2Basis {
 public:
  enum { kN = 9 };
  B2Basis() : pos_(0), n_(0) {
    for (int i = 0; i < kN; ++i) { rev_[i] = 0; t_[i] = 0; }
  }
  void Beobachte(long long rev, long long t) {
    rev_[pos_] = rev; t_[pos_] = t;
    pos_ = (pos_ + 1) % kN;
    if (n_ < kN) ++n_;
  }
  bool Prognose(long long rev, long long* f) const {
    if (n_ < 2) return false;
    const int neu = (pos_ + kN - 1) % kN;
    const int alt = (pos_ + kN - n_) % kN;
    const long long dr = rev_[neu] - rev_[alt];
    if (dr <= 0) return false;
    const long long drev = rev - rev_[neu];
    const long long dt = t_[neu] - t_[alt];
    const long long kLim = 1LL << 31;
    if (drev >= kLim || drev <= -kLim || dt >= kLim || dt <= -kLim) return false;
    *f = t_[neu] + Fdiv(drev * dt, dr);
    return true;
  }
  void Reset() {
    pos_ = 0; n_ = 0;
    for (int i = 0; i < kN; ++i) { rev_[i] = 0; t_[i] = 0; }
  }
 private:
  long long rev_[kN], t_[kN];
  int pos_, n_;
};

// Transforms Wikipedia Metadata (Header compaction, Page-ID deltas, B1 Rev-ID forecast, compact timestamps, and boilerplate tags).
bool Transform(FILE* in, int len, FILE* out, int flags = TRANSFORM_ALL);

// Streaming decoder that wraps a stream source (Dictionary or FILE)
class StreamingDecoder {
 public:
  StreamingDecoder(preprocessor::Dictionary* dict, FILE* in, int flags = TRANSFORM_ALL);

  int GetByte();

 private:
  bool Fill();
  bool StartsWith(const char* prefix) const;
  bool SubstrEquals(size_t offset, const char* str) const;
  void Consume(size_t n);
  void PushString(const std::string& s);

  preprocessor::Dictionary* dict_;
  FILE* in_;
  int flags_;
  unsigned long long prev_id_;
  unsigned long long curr_revid_;
  long long dec_prev_sec_;
  bool first_byte_;
  bool eof_;
  B1Interp dec_b1_;
  B2Basis dec_b2_;
  std::deque<char> window_;
  std::deque<int> buf_;
};

}  // namespace metadata

#endif  // METADATA_H_
