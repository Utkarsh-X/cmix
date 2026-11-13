#ifndef MORPH_CONTEXT_H
#define MORPH_CONTEXT_H

#include "context.h"
#include <array>
#include <cstdint>

class MorphContext : public Context {
 public:
  explicit MorphContext(const unsigned int& bit_context);
  void Update() override;
  bool IsEqual(Context* c) override;

 private:
  const unsigned int& byte_;

  uint8_t boundary_type_ = 1;
  uint16_t word_len_ = 0;
  uint64_t current_prefix_hash_ = 0;
  std::array<char, 32> word_buf_{};

  uint8_t suffix_state_ = 0;
  uint8_t best_suffix_node_ = 0;

  std::array<uint64_t, 16> stem_cache_{};
  std::array<uint8_t, 16> stem_len_cache_{};
  uint8_t cache_size_ = 0;
  bool stem_hit_ = false;

  void InsertStem(uint64_t hash, uint8_t len);
};

#endif  // MORPH_CONTEXT_H
