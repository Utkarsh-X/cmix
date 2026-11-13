#ifndef CITATION_CONTEXT_H
#define CITATION_CONTEXT_H

#include "context.h"

// CitationContext: Dense 64-state structural FSM tracking Wikipedia Citation & URL syntax.
// Conditioned states:
// - State 0: Outside citation/URL (default mode 0)
// - Mode 1: Inside <ref> or &lt;ref attributes (dist 0..7 -> states 1..8)
// - Mode 2: Inside ref body content (dist 0..7 -> states 9..16)
// - Mode 3: Inside {{cite ... template name (dist 0..7 -> states 17..24)
// - Mode 4: Inside cite parameter name |param= (dist 0..7 -> states 25..32)
// - Mode 5: Inside cite parameter value (dist 0..7 -> states 33..40)
// - Mode 6: Inside URL protocol http: / https: (dist 0..7 -> states 41..48)
// - Mode 7: Inside URL host domain (dist 0..7 -> states 49..56)
// - Mode 8: Inside URL path/query (dist 0..6 -> states 57..63)
// Total Size: 64 discrete states (fits in 6-bit index 0..63).
class CitationContext : public Context {
 public:
  explicit CitationContext(const unsigned int& bit_context);
  void Update() override;
  bool IsEqual(Context* c) override;

 private:
  bool EndsWith(int len, const char* str) const;
  bool EndsWithInsensitive(int len, const char* str) const;

  const unsigned int& byte_;
  int mode_ = 0;
  int dist_ = 0;
  int ref_depth_ = 0;
  int curly_depth_ = 0;
  int bracket_depth_ = 0;
  bool in_cite_ = false;
  int return_mode_ = 0;
  unsigned char prev_byte_ = 0;
  unsigned char h_[16];
};

#endif
