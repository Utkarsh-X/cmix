#ifndef GRAMMAR_MATCH_H
#define GRAMMAR_MATCH_H

#include "model.h"

#include <array>

class GrammarMatch : public Model {
 public:
  GrammarMatch(const unsigned int& bit_context, int limit = 200, float delta = 0.5f);
  ~GrammarMatch() override;

  const std::valarray<float>& Predict() override;
  void Perceive(int bit) override;
  void ByteUpdate() override;

  bool HaveExpectation() const { return have_expectation_; }
  unsigned char ExpectedByte() const { return expected_byte_; }
  int StateIndex() const { return state_; }

 private:
  enum Family {
    kFamilyPiped = 0,
    kFamilyPipedPostCopy = 1,
    kFamilyEcho = 2,
    kFamilyClose2 = 3,
    kFamilyCurly2 = 4,
    kFamilyOpen2 = 5
  };
  enum BracketState { kBrIdle, kBrTarget, kBrLabel, kBrPostCopy, kBrClose2, kBrScan };

  void ParseByte(unsigned char c);
  void BracketAdvance(unsigned char c);
  void EchoAdvance(unsigned char c);
  void FinishTitle();
  void ExportExpectation();

  static inline bool AbortClass(unsigned char c) {
    return c <= 0x05 || c == 0x08 || c == '\n' || (c >= 0x0E && c <= 0x10);
  }

  static const int kTitleCap = 256;
  static const int kTargetCap = 512;
  static const int kEchoArm = 2;

  const unsigned int& bit_context_;

  unsigned char expected_byte_ = 0;
  unsigned char bit_pos_ = 128;
  bool have_expectation_ = false;
  int state_ = 0;
  int limit_;
  float delta_, divisor_;
  std::array<float, 256> predictions_;
  std::array<int, 256> counts_;

  unsigned long long recent_ = 0;
  bool in_title_ = false;
  int tbuf_len_ = 0;
  int title_len_ = 0;
  int echo_k_ = 0;
  std::array<unsigned char, kTitleCap> tbuf_;
  std::array<unsigned char, kTitleCap> title_;
  std::array<int, kTitleCap> fail_;

  int br_state_ = kBrIdle;
  int tgt_len_ = 0;
  int label_j_ = 0;
  int label_pos_ = 0;
  int curly_depth_ = 0;
  bool curly_close2_ = false;
  unsigned char open2_expect_ = 0;
  unsigned char prev_c_ = 0;
  std::array<unsigned char, kTargetCap> tgt_;
};

#endif  // GRAMMAR_MATCH_H
