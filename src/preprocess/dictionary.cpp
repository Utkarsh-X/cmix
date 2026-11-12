#include "dictionary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

namespace preprocessor {

namespace {

const unsigned char kCapitalized = 0x40;
const unsigned char kUppercase = 0x07;
const unsigned char kEndUpper = 0x06;
const unsigned char kEscape = 0x0C;
const unsigned char kQuote = 0x08;
const char* kQuoteStr = "&quot;";
const unsigned char kAmp = 0x01;
const char* kAmpStr = "&amp;";
const unsigned char kLt = 0x02;
const char* kLtStr = "&lt;";
const unsigned char kGt = 0x03;
const char* kGtStr = "&gt;";

void EncodeByte(unsigned char c, FILE* output) {
  if (c == kEndUpper || c == kEscape || c == kUppercase ||
      c == kCapitalized || c == kQuote || c == kAmp || c == kLt || c == kGt || c >= 0x80) {
    putc(kEscape, output);
  }
  putc(c, output);
}

void EncodeBytes(unsigned int bytes, FILE* output) {
  putc((unsigned char)(bytes & 0xFF), output);
  if (bytes & 0xFF00) {
    putc((unsigned char)((bytes & 0xFF00) >> 8), output);
  } else {
    return;
  }
  if (bytes & 0xFF0000) {
    putc((unsigned char)((bytes & 0xFF0000) >> 16), output);
  }
}

}  // namespace

FastWordMap::FastWordMap()
    : blob_(nullptr), blob_len_(0), blob_cap_(0),
      off_(nullptr), len_(nullptr), code_(nullptr),
      n_(0), cap_(0), tab_(nullptr), rtab_(nullptr), mask_(0) {}

FastWordMap::~FastWordMap() {
  if (blob_) { free(blob_); blob_ = nullptr; }
  if (off_)  { free(off_);  off_  = nullptr; }
  if (len_)  { free(len_);  len_  = nullptr; }
  if (code_) { free(code_); code_ = nullptr; }
  if (tab_)  { free(tab_);  tab_  = nullptr; }
  if (rtab_) { free(rtab_); rtab_ = nullptr; }
}

void FastWordMap::ReserveExact(unsigned int words, unsigned int blob_bytes,
    bool encode, bool decode) {
  cap_ = words ? words : 1;
  off_  = (unsigned int*)malloc(sizeof(unsigned int) * cap_);
  len_  = (unsigned char*)malloc(sizeof(unsigned char) * cap_);
  code_ = (unsigned int*)malloc(sizeof(unsigned int) * cap_);
  blob_cap_ = blob_bytes ? blob_bytes : 1;
  blob_ = (char*)malloc(blob_cap_);

  mask_ = 1;
  while (mask_ < 2 * cap_) mask_ <<= 1;
  mask_ -= 1;

  if (encode) {
    tab_ = (unsigned int*)malloc(sizeof(unsigned int) * (mask_ + 1));
    memset(tab_, 0, sizeof(unsigned int) * (mask_ + 1));
  }
  if (decode) {
    rtab_ = (unsigned int*)malloc(sizeof(unsigned int) * (mask_ + 1));
    memset(rtab_, 0, sizeof(unsigned int) * (mask_ + 1));
  }
}

void FastWordMap::Add(const char* w, unsigned int len, unsigned int code,
    bool encode, bool decode) {
  memcpy(blob_ + blob_len_, w, len);
  off_[n_] = blob_len_;
  len_[n_] = (unsigned char)len;
  code_[n_] = code;
  blob_len_ += len;
  ++n_;

  if (encode && tab_) {
    unsigned int h = HashWord(w, len) & mask_;
    while (tab_[h]) h = (h + 1) & mask_;
    tab_[h] = n_;  // 1-based index
  }
  if (decode && rtab_) {
    unsigned int h = HashCode(code) & mask_;
    while (rtab_[h]) h = (h + 1) & mask_;
    rtab_[h] = n_;
  }
}

bool FastWordMap::Find(const char* w, unsigned int len, unsigned int* code) const {
  if (!tab_) return false;
  unsigned int h = HashWord(w, len) & mask_;
  while (tab_[h]) {
    const unsigned int i = tab_[h] - 1;
    if (len_[i] == len && memcmp(blob_ + off_[i], w, len) == 0) {
      *code = code_[i];
      return true;
    }
    h = (h + 1) & mask_;
  }
  return false;
}

bool FastWordMap::FindCode(unsigned int code, const char** w, unsigned int* len) const {
  if (!rtab_) return false;
  unsigned int h = HashCode(code) & mask_;
  while (rtab_[h]) {
    const unsigned int i = rtab_[h] - 1;
    if (code_[i] == code) {
      *w = blob_ + off_[i];
      *len = len_[i];
      return true;
    }
    h = (h + 1) & mask_;
  }
  return false;
}

Dictionary::Dictionary(FILE* dictionary, bool encode, bool decode) {
  fseek(dictionary, 0L, SEEK_END);
  unsigned long long len = ftell(dictionary);
  fseek(dictionary, 0L, SEEK_SET);

  // First pass: exact count of words and characters
  unsigned int word_count = 0;
  unsigned int blob_chars = 0;
  unsigned int cur_len = 0;
  for (unsigned pos = 0; pos < len; ++pos) {
    int c = getc(dictionary);
    if (c >= 'a' && c <= 'z') {
      ++cur_len;
    } else if (cur_len > 0) {
      ++word_count;
      blob_chars += cur_len;
      cur_len = 0;
    }
  }
  if (cur_len > 0) {
    ++word_count;
    blob_chars += cur_len;
  }
  fseek(dictionary, 0L, SEEK_SET);

  word_map_.ReserveExact(word_count, blob_chars, encode, decode);

  std::string line;
  int line_count = 0;
  const int kBoundary1 = 80, kBoundary2 = kBoundary1 + 3840,
      kBoundary3 = kBoundary2 + 40960, kBoundary4 = kBoundary3 + 81920;
  for (unsigned pos = 0; pos < len; ++pos) {
    unsigned char c = getc(dictionary);
    if (c >= 'a' && c <= 'z') {
      line += c;
    } else if (!line.empty()) {
      if (line.size() > longest_word_) longest_word_ = (unsigned int)line.size();
      unsigned int bytes;
      if (line_count < kBoundary1) {
        bytes = 0x80 + line_count;
      } else if (line_count < kBoundary2) {
        bytes = 0xD0 + ((line_count - kBoundary1) / 80);
        bytes += (0x80 + ((line_count - kBoundary1) % 80)) << 8;
      } else if (line_count < kBoundary3) {
        bytes = 0xF0 + (((line_count - kBoundary2) / 80) / 32);
        bytes += (0xD0 + (((line_count - kBoundary2) / 80) % 32)) << 8;
        bytes += (0x80 + ((line_count - kBoundary2) % 80)) << 16;
      } else if (line_count < kBoundary4) {
        bytes = 0xD0 + (((line_count - kBoundary3) / 80) / 32);
        bytes += (0xD0 + (((line_count - kBoundary3) / 80) % 32)) << 8;
        bytes += (0x80 + ((line_count - kBoundary3) % 80)) << 16;
      }
      word_map_.Add(line.data(), (unsigned int)line.size(), bytes, encode, decode);
      ++line_count;
      line.clear();
    }
  }
}

void Dictionary::Encode(FILE* input, int len, FILE* output) {
  std::string word;
  int num_upper = 0, num_lower = 0;
  bool after_amp = false;
  for (int pos = 0; pos < len; ++pos) {
    unsigned char c = getc(input);
    if (c == ';' && after_amp && num_upper == 0) {
      if (word == "quot") {
        putc(kQuote, output);
        num_lower = 0;
        word.clear();
        after_amp = false;
        continue;
      } else if (word == "amp") {
        putc(kAmp, output);
        num_lower = 0;
        word.clear();
        after_amp = false;
        continue;
      } else if (word == "lt") {
        putc(kLt, output);
        num_lower = 0;
        word.clear();
        after_amp = false;
        continue;
      } else if (word == "gt") {
        putc(kGt, output);
        num_lower = 0;
        word.clear();
        after_amp = false;
        continue;
      }
    }
    bool advance = false;
    if (word.size() > longest_word_) {
      advance = true;
    } else if (c >= 'a' && c <= 'z') {
      if (num_upper > 1) {
        advance = true;
      } else {
        ++num_lower;
        word += c;
      }
    } else if (c >= 'A' && c <= 'Z') {
      if (num_lower > 0) {
        advance = true;
      } else {
        ++num_upper;
        word += (c - 'A' + 'a');
      }
    } else {
      advance = true;
    }
    if (pos == len - 1 && !advance) {
      EncodeWord(word, num_upper, false, output);
    }
    if (advance) {
      if (word.empty()) {
        EncodeByte(c, output);
        after_amp = (c == '&');
      } else {
        bool next_lower = (c >= 'a' && c <= 'z');
        EncodeWord(word, num_upper, next_lower, output);
        num_lower = 0;
        num_upper = 0;
        word.clear();
        if (next_lower) {
          ++num_lower;
          word += c;
          after_amp = false;
        } else if (c >= 'A' && c <= 'Z') {
          ++num_upper;
          word += (c - 'A' + 'a');
          after_amp = false;
        } else {
          EncodeByte(c, output);
          after_amp = (c == '&');
        }
        if (pos == len - 1 && !word.empty()) {
          EncodeWord(word, num_upper, false, output);
        }
      }
    }
  }
}

void Dictionary::EncodeWord(const std::string& word, int num_upper,
    bool next_lower, FILE* output) {
  if (num_upper > 1) putc(kUppercase, output);
  else if (num_upper == 1) putc(kCapitalized, output);
  unsigned int bytes;
  if (word_map_.Find(word.data(), (unsigned int)word.size(), &bytes)) {
    EncodeBytes(bytes, output);
  } else if (!EncodeSubstring(word, output)) {
    for (unsigned int i = 0; i < word.size(); ++i) {
      putc((unsigned char)word[i], output);
    }
  }
  if (num_upper > 1 && next_lower) {
    putc(kEndUpper, output);
  }
}

bool Dictionary::EncodeSubstring(const std::string& word, FILE* output) {
  if (word.size() <= 7) return false;
  unsigned int size = (unsigned int)word.size() - 1;
  if (size > longest_word_) size = longest_word_;
  std::string suffix = word.substr(word.size() - size, size);
  while (suffix.size() >= 7) {
    unsigned int fast_code;
    if (word_map_.Find(suffix.data(), (unsigned int)suffix.size(), &fast_code)) {
      for (unsigned int i = 0; i < word.size() - suffix.size(); ++i) {
        putc((unsigned char)word[i], output);
      }
      EncodeBytes(fast_code, output);
      return true;
    }
    suffix.erase(0, 1);
  }
  std::string prefix = word.substr(0, size);
  while (prefix.size() >= 7) {
    unsigned int fast_code;
    if (word_map_.Find(prefix.data(), (unsigned int)prefix.size(), &fast_code)) {
      EncodeBytes(fast_code, output);
      for (unsigned int i = prefix.size(); i < word.size(); ++i) {
        putc((unsigned char)word[i], output);
      }
      return true;
    }
    prefix.erase(prefix.size() - 1, 1);
  }
  return false;
}

int Dictionary::Decode(FILE* input) {
  while (buf_head_ == buf_tail_) {
    if (!AddToBuffer(input)) return -1;
  }
  unsigned char next = circular_buf_[buf_head_];
  buf_head_ = (buf_head_ + 1) & 1023;
  return next;
}

bool Dictionary::AddToBuffer(FILE* input) {
  int in_c = getc(input);
  if (in_c == EOF) return false;
  unsigned char c = static_cast<unsigned char>(in_c);
  if (c == kEscape) {
    decode_upper_ = false;
    int next_c = getc(input);
    if (next_c != EOF) {
      PushBuf(static_cast<unsigned char>(next_c));
    }
  } else if (c == kQuote) {
    for (int i = 1; i < 6; ++i) {
      PushBuf(kQuoteStr[i]);
    }
  } else if (c == kAmp) {
    for (int i = 1; i < 5; ++i) {
      PushBuf(kAmpStr[i]);
    }
  } else if (c == kLt) {
    for (int i = 1; i < 4; ++i) {
      PushBuf(kLtStr[i]);
    }
  } else if (c == kGt) {
    for (int i = 1; i < 4; ++i) {
      PushBuf(kGtStr[i]);
    }
  } else if (c == kUppercase) {
    decode_upper_ = true;
  } else if (c == kCapitalized) {
    decode_capital_ = true;
  } else if (c == kEndUpper) {
    decode_upper_ = false;
  } else if (c >= 0x80) {
    unsigned int bytes = c;
    if (c > 0xCF) {
      int next_c = getc(input);
      if (next_c != EOF) {
        c = static_cast<unsigned char>(next_c);
        bytes += c << 8;
        if (c > 0xCF) {
          next_c = getc(input);
          if (next_c != EOF) {
            c = static_cast<unsigned char>(next_c);
            bytes += c << 16;
          }
        }
      }
    }
    const char* word_str = nullptr;
    unsigned int word_len = 0;
    if (word_map_.FindCode(bytes, &word_str, &word_len)) {
      for (unsigned int i = 0; i < word_len; ++i) {
        unsigned char wc = static_cast<unsigned char>(word_str[i]);
        if (i == 0 && decode_capital_) {
          wc = (wc - 'a') + 'A';
          decode_capital_ = false;
        }
        if (decode_upper_) {
          wc = (wc - 'a') + 'A';
        }
        PushBuf(wc);
      }
    }
  } else {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
      decode_upper_ = false;
    }
    if (decode_capital_ || decode_upper_) {
      c = (c - 'a') + 'A';
    }
    if (decode_capital_) decode_capital_ = false;
    PushBuf(c);
  }
  return true;
}

}  // namespace preprocessor
