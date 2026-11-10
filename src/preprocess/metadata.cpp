#include "metadata.h"
#include "dictionary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace metadata {

bool Transform(FILE* in, int len, FILE* out, int flags) {
  unsigned long long prev_id = 0;
  int c;
  int pos = 0;
  int count_id = 0;
  const std::string title_tag = "</title>";
  size_t title_match = 0;

  while (pos < len && (c = getc(in)) != EOF) {
    ++pos;
    putc(c, out);

    if ((flags & TRANSFORM_PAGE_IDS) && c == title_tag[title_match]) {
      ++title_match;
      if (title_match == title_tag.size()) {
        title_match = 0;
        std::vector<int> ws;
        while (pos < len && (c = getc(in)) != EOF) {
          ++pos;
          if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ws.push_back(c);
          } else {
            break;
          }
        }
        if (c == '<') {
          int c1 = getc(in); ++pos;
          int c2 = getc(in); ++pos;
          int c3 = getc(in); ++pos;
          if (c1 == 'i' && c2 == 'd' && c3 == '>') {
            std::string id_str;
            while (pos < len && (c = getc(in)) != EOF) {
              ++pos;
              if (c >= '0' && c <= '9') {
                id_str += static_cast<char>(c);
              } else {
                break;
              }
            }
            if (c == '<') {
              int d1 = getc(in); ++pos;
              int d2 = getc(in); ++pos;
              int d3 = getc(in); ++pos;
              int d4 = getc(in); ++pos;
              if (d1 == '/' && d2 == 'i' && d3 == 'd' && d4 == '>') {
                for (int w : ws) putc(w, out);
                fputs("<id>", out);
                unsigned long long curr_id = std::stoull(id_str);
                unsigned long long delta = curr_id - prev_id;
                prev_id = curr_id;
                std::string delta_str = std::to_string(delta);
                fputs(delta_str.c_str(), out);
                fputs("</id>", out);
                ++count_id;
                continue;
              } else {
                for (int w : ws) putc(w, out);
                fputs("<id>", out);
                fputs(id_str.c_str(), out);
                putc(c, out);
                if (d1 != EOF) putc(d1, out);
                if (d2 != EOF) putc(d2, out);
                if (d3 != EOF) putc(d3, out);
                if (d4 != EOF) putc(d4, out);
                continue;
              }
            } else {
              for (int w : ws) putc(w, out);
              fputs("<id>", out);
              fputs(id_str.c_str(), out);
              if (c != EOF) putc(c, out);
              continue;
            }
          } else {
            for (int w : ws) putc(w, out);
            putc('<', out);
            if (c1 != EOF) putc(c1, out);
            if (c2 != EOF) putc(c2, out);
            if (c3 != EOF) putc(c3, out);
            continue;
          }
        } else {
          for (int w : ws) putc(w, out);
          if (c != EOF) putc(c, out);
          continue;
        }
      }
    } else {
      title_match = (c == title_tag[0]) ? 1 : 0;
    }
  }
  return count_id > 0;
}

StreamingDecoder::StreamingDecoder(preprocessor::Dictionary* dict, FILE* in, int flags)
    : dict_(dict), in_(in), flags_(flags), prev_id_(0), title_match_(0) {}

int StreamingDecoder::GetByte() {
  while (buf_.empty()) {
    if (!Fill()) return -1;
  }
  int c = buf_.front();
  buf_.pop_front();
  return c;
}

bool StreamingDecoder::Fill() {
  auto next_byte = [this]() -> int {
    if (dict_ != NULL) return dict_->Decode(in_);
    return getc(in_);
  };

  int c = next_byte();
  if (c == EOF) return false;

  buf_.push_back(c);

  const std::string title_tag = "</title>";
  if ((flags_ & TRANSFORM_PAGE_IDS) && c == title_tag[title_match_]) {
    ++title_match_;
    if (title_match_ == title_tag.size()) {
      title_match_ = 0;
      std::vector<int> ws;
      while (true) {
        c = next_byte();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
          ws.push_back(c);
        } else {
          break;
        }
      }
      if (c == '<') {
        int c1 = next_byte();
        int c2 = next_byte();
        int c3 = next_byte();
        if (c1 == 'i' && c2 == 'd' && c3 == '>') {
          std::string delta_str;
          while (true) {
            c = next_byte();
            if (c >= '0' && c <= '9') {
              delta_str += static_cast<char>(c);
            } else {
              break;
            }
          }
          if (c == '<') {
            int d1 = next_byte();
            int d2 = next_byte();
            int d3 = next_byte();
            int d4 = next_byte();
            if (d1 == '/' && d2 == 'i' && d3 == 'd' && d4 == '>') {
              for (int w : ws) buf_.push_back(w);
              const std::string open_id = "<id>";
              for (char ch : open_id) buf_.push_back(ch);
              unsigned long long delta = std::stoull(delta_str);
              unsigned long long curr_id = prev_id_ + delta;
              prev_id_ = curr_id;
              std::string id_str = std::to_string(curr_id);
              for (char ch : id_str) buf_.push_back(ch);
              const std::string close_id = "</id>";
              for (char ch : close_id) buf_.push_back(ch);
              return true;
            } else {
              for (int w : ws) buf_.push_back(w);
              const std::string open_id = "<id>";
              for (char ch : open_id) buf_.push_back(ch);
              for (char ch : delta_str) buf_.push_back(ch);
              buf_.push_back(c);
              if (d1 != EOF) buf_.push_back(d1);
              if (d2 != EOF) buf_.push_back(d2);
              if (d3 != EOF) buf_.push_back(d3);
              if (d4 != EOF) buf_.push_back(d4);
              return true;
            }
          } else {
            for (int w : ws) buf_.push_back(w);
            const std::string open_id = "<id>";
            for (char ch : open_id) buf_.push_back(ch);
            for (char ch : delta_str) buf_.push_back(ch);
            if (c != EOF) buf_.push_back(c);
            return true;
          }
        } else {
          for (int w : ws) buf_.push_back(w);
          buf_.push_back('<');
          if (c1 != EOF) buf_.push_back(c1);
          if (c2 != EOF) buf_.push_back(c2);
          if (c3 != EOF) buf_.push_back(c3);
          return true;
        }
      } else {
        for (int w : ws) buf_.push_back(w);
        if (c != EOF) buf_.push_back(c);
        return true;
      }
    }
    return true;
  } else {
    title_match_ = (c == title_tag[0]) ? 1 : 0;
  }
  return true;
}

}  // namespace metadata
