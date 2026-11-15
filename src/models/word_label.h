// Origin: zmix 1.0 (James Byrne), GPL-3.0-or-later.
// Ported and adapted for CMIX.
#ifndef WORD_LABEL_H_
#define WORD_LABEL_H_

#include <cstdint>
#include <memory>

class WordLabel {
 public:
  WordLabel();
  ~WordLabel();

  // Non-copyable, non-movable
  WordLabel(const WordLabel&) = delete;
  WordLabel& operator=(const WordLabel&) = delete;

  // Hot: layer-0 stretched logit for THIS coded bit.
  // Returns the raw logit in [-20, 20] on payload bits (bits 6..0) of a WRT
  // codeword byte and EXACTLY 0.0f on quiescent bits (neutral input).
  float Forward();

  // Hot: trains on the observed bit and advances the causal codeword parser.
  // Call once per bit, after the bit is known (in Predictor::Perceive).
  void Perceive(int bit);

  // Resets all learned parameters and parse state (e.g. for independent blocks).
  void Reset();

 private:
  struct State;
  std::unique_ptr<State> state_;
};

#endif  // WORD_LABEL_H_
