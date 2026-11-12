#ifndef METADATA_H_
#define METADATA_H_

#include <stdio.h>
#include <deque>
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
  TRANSFORM_ALL = (TRANSFORM_PAGE_IDS | TRANSFORM_TIMESTAMPS | TRANSFORM_BOILERPLATE | TRANSFORM_REV_IDS)
};

// Transforms Wikipedia Metadata (Header compaction, Page-ID deltas, B1 Rev-ID forecast, compact timestamps, and boilerplate tags).
// Writes the transformed text stream to `out`.
// Returns true if any metadata was transformed; false if no Wikipedia metadata found.
bool Transform(FILE* in, int len, FILE* out, int flags = TRANSFORM_ALL);

// EXP-080: B2 Basis for monotonic timestamp extrapolation
class B2Basis {
 public:
  enum { kN = 9 };
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
    long long prod = drev * dt;
    long long q = prod / dr, r = prod % dr;
    if (r != 0 && ((r < 0) != (dr < 0))) --q;
    *f = t_[neu] + q;
    return true;
  }
  void Reset() {
    pos_ = 0; n_ = 0;
    for (int i = 0; i < kN; ++i) { rev_[i] = 0; t_[i] = 0; }
  }

 private:
  long long rev_[kN] = {0}, t_[kN] = {0};
  int pos_ = 0, n_ = 0;
};

// Streaming decoder that wraps a stream source (Dictionary or FILE)
// and dynamically expands header, delta Page-IDs, B1 Rev-IDs, compact timestamps, and boilerplate tags back to canonical text.
class StreamingDecoder {
 public:
  StreamingDecoder(preprocessor::Dictionary* dict, FILE* in, int flags = TRANSFORM_ALL);

  int GetByte();

 private:
  bool Fill();
  bool StartsWith(const std::string& prefix) const;
  bool SubstrEquals(size_t offset, const std::string& str) const;
  void Consume(size_t n);
  void PushString(const std::string& s);

  preprocessor::Dictionary* dict_;
  FILE* in_;
  int flags_;
  unsigned long long prev_id_;
  unsigned long long last_b1_pid_;
  unsigned long long last_b1_rid_;
  unsigned long long curr_revid_;
  long long dec_prev_sec_;
  B2Basis dec_b2_;
  bool first_byte_;
  bool eof_;
  std::deque<char> window_;
  std::deque<int> buf_;
};

}  // namespace metadata

#endif  // METADATA_H_
