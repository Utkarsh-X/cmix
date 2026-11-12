#ifndef DICTIONARY_H
#define DICTIONARY_H

#include <stdio.h>
#include <string>
#include <cstdint>

namespace preprocessor {

class FastWordMap {
 public:
  FastWordMap();
  ~FastWordMap();

  void ReserveExact(unsigned int words, unsigned int blob_bytes, bool encode, bool decode);
  void Add(const char* w, unsigned int len, unsigned int code, bool encode, bool decode);
  bool Find(const char* w, unsigned int len, unsigned int* code) const;
  bool FindCode(unsigned int code, const char** w, unsigned int* len) const;

 private:
  static inline unsigned int HashWord(const char* w, unsigned int len) {
    unsigned int h = 2166136261u;
    for (unsigned int i = 0; i < len; ++i) {
      h ^= static_cast<unsigned char>(w[i]);
      h *= 16777619u;
    }
    return h;
  }

  static inline unsigned int HashCode(unsigned int c) {
    unsigned int h = c * 2654435761u;
    return h ^ (h >> 15);
  }

  char* blob_ = nullptr;
  unsigned int blob_len_ = 0;
  unsigned int blob_cap_ = 0;

  unsigned int* off_ = nullptr;
  unsigned char* len_ = nullptr;
  unsigned int* code_ = nullptr;
  unsigned int n_ = 0;
  unsigned int cap_ = 0;

  unsigned int* tab_ = nullptr;
  unsigned int* rtab_ = nullptr;
  unsigned int mask_ = 0;
};

class Dictionary {
 public:
  Dictionary(FILE* dictionary, bool encode, bool decode);
  void Encode(FILE* input, int len, FILE* output);
  int Decode(FILE* input);

 private:
  void EncodeWord(const std::string& word, int num_upper, bool next_lower,
      FILE* output);
  bool EncodeSubstring(const std::string& word, FILE* output);
  bool AddToBuffer(FILE* input);

  inline void PushBuf(unsigned char c) {
    circular_buf_[buf_tail_] = c;
    buf_tail_ = (buf_tail_ + 1) & 1023;
  }

  FastWordMap word_map_;
  unsigned char circular_buf_[1024];
  uint32_t buf_head_ = 0;
  uint32_t buf_tail_ = 0;
  bool decode_upper_ = false, decode_capital_ = false;
  unsigned int longest_word_ = 0;
};

}  // namespace preprocessor

#endif  // DICTIONARY_H
