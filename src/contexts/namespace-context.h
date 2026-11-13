#ifndef NAMESPACE_CONTEXT_H
#define NAMESPACE_CONTEXT_H

#include "context.h"
#include <cstdint>

// NamespaceContext: Dense 64-state structural FSM tracking Wikipedia Category, Media, & Interwiki syntax.
// Conditioned states:
// - State 0: Normal Text (mode 0)
// - States 1..8: Category Prefix [[Category: (mode 1, dist 0..7)
// - States 9..16: Category Title (mode 2, dist 0..7)
// - States 17..22: Category Sort Key |... (mode 3, dist 0..5)
// - States 23..26: Interwiki Lang Code (mode 4, dist 0..3)
// - State 27: Interwiki Colon : (mode 5)
// - States 28..35: Interwiki Title (mode 6, dist 0..7)
// - States 36..37: Interwiki Delim / Block (mode 7, dist 0..1)
// - States 38..43: Media Prefix [[Image: / [[File: (mode 8, dist 0..5)
// - States 44..51: Media Target / Filename (mode 9, dist 0..7)
// - States 52..57: Media Format Attribute |thumb, |right (mode 10, dist 0..5)
// - States 58..63: Media Caption / Content (mode 11, dist 0..5)
// Total Size: strictly 64 discrete states (0..63).
class NamespaceContext : public Context {
 public:
  explicit NamespaceContext(const unsigned int& bit_context);
  void Update() override;
  bool IsEqual(Context* c) override;

 private:
  bool EndsWith(int len, const char* str) const;
  bool EndsWithInsensitive(int len, const char* str) const;

  const unsigned int& byte_;
  int mode_ = 0;
  int dist_ = 0;
  int bracket_depth_ = 0;
  unsigned char prev_byte_ = 0;
  int prefix_step_ = 0;
  int cand_lang_len_ = 0;
  bool in_iw_block_ = false;
  unsigned char h_[16];
};

#endif  // NAMESPACE_CONTEXT_H
