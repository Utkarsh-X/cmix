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
  int count_id = 0, count_ts = 0;
  const std::string title_tag = "</title>";
  const std::string ts_tag = "<timestamp>";
  size_t title_match = 0;
  size_t ts_match = 0;

  while (pos < len && (c = getc(in)) != EOF) {
    ++pos;

    // 1. Check for <timestamp>
    if ((flags & TRANSFORM_TIMESTAMPS) && c == ts_tag[ts_match]) {
      ++ts_match;
      if (ts_match == ts_tag.size()) {
        ts_match = 0;
        char ts_buf[20];
        int read_n = 0;
        while (read_n < 20 && pos < len && (c = getc(in)) != EOF) {
          ++pos;
          ts_buf[read_n++] = static_cast<char>(c);
        }
        char end_buf[12];
        int end_n = 0;
        while (end_n < 12 && pos < len && (c = getc(in)) != EOF) {
          ++pos;
          end_buf[end_n++] = static_cast<char>(c);
        }
        bool valid = false;
        if (read_n == 20 && end_n == 12) {
          if (ts_buf[0] == '2' && ts_buf[1] == '0' &&
              ts_buf[4] == '-' && ts_buf[7] == '-' &&
              ts_buf[10] == 'T' &&
              ts_buf[13] == ':' && ts_buf[16] == ':' && ts_buf[19] == 'Z' &&
              strncmp(end_buf, "</timestamp>", 12) == 0) {
            valid = true;
          }
        }
        if (valid) {
          fputs("<timestamp>", out);
          putc(ts_buf[2], out); putc(ts_buf[3], out); // YY
          putc(ts_buf[5], out); putc(ts_buf[6], out); // MM
          putc(ts_buf[8], out); putc(ts_buf[9], out); // DD
          putc(ts_buf[11], out); putc(ts_buf[12], out); // HH
          putc(ts_buf[14], out); putc(ts_buf[15], out); // MM
          putc(ts_buf[17], out); putc(ts_buf[18], out); // SS
          fputs("</timestamp>", out);
          ++count_ts;
          continue;
        } else {
          fputs("<timestamp>", out);
          for (int i = 0; i < read_n; ++i) putc(ts_buf[i], out);
          for (int i = 0; i < end_n; ++i) putc(end_buf[i], out);
          continue;
        }
      }
      continue;
    } else {
      if (ts_match > 0) {
        for (size_t i = 0; i < ts_match; ++i) putc(ts_tag[i], out);
        ts_match = (c == ts_tag[0]) ? 1 : 0;
        if (ts_match > 0) continue;
      }
    }

    putc(c, out);

    // 2. Check for </title>
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
  return (count_id > 0 || count_ts > 0);
}

StreamingDecoder::StreamingDecoder(preprocessor::Dictionary* dict, FILE* in, int flags)
    : dict_(dict), in_(in), flags_(flags), prev_id_(0), title_match_(0), ts_match_(0) {}

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

  const std::string ts_tag = "<timestamp>";
  const std::string title_tag = "</title>";

  // 1. Check for <timestamp>
  if ((flags_ & TRANSFORM_TIMESTAMPS) && c == ts_tag[ts_match_]) {
    ++ts_match_;
    if (ts_match_ == ts_tag.size()) {
      ts_match_ = 0;
      char ts_buf[12];
      int read_n = 0;
      while (read_n < 12) {
        c = next_byte();
        if (c == EOF) break;
        if (c >= '0' && c <= '9') {
          ts_buf[read_n++] = static_cast<char>(c);
        } else {
          break;
        }
      }
      char end_buf[12];
      int end_n = 0;
      if (read_n == 12) {
        c = next_byte();
      }
      if (c == '<') {
        end_buf[end_n++] = (char)c;
        while (end_n < 12) {
          c = next_byte();
          if (c == EOF) break;
          end_buf[end_n++] = static_cast<char>(c);
        }
      }
      if (read_n == 12 && end_n == 12 && strncmp(end_buf, "</timestamp>", 12) == 0) {
        for (char ch : ts_tag) buf_.push_back(ch);
        buf_.push_back('2'); buf_.push_back('0');
        buf_.push_back(ts_buf[0]); buf_.push_back(ts_buf[1]); buf_.push_back('-');
        buf_.push_back(ts_buf[2]); buf_.push_back(ts_buf[3]); buf_.push_back('-');
        buf_.push_back(ts_buf[4]); buf_.push_back(ts_buf[5]); buf_.push_back('T');
        buf_.push_back(ts_buf[6]); buf_.push_back(ts_buf[7]); buf_.push_back(':');
        buf_.push_back(ts_buf[8]); buf_.push_back(ts_buf[9]); buf_.push_back(':');
        buf_.push_back(ts_buf[10]); buf_.push_back(ts_buf[11]); buf_.push_back('Z');
        const std::string end_ts = "</timestamp>";
        for (char ch : end_ts) buf_.push_back(ch);
        return true;
      } else {
        for (char ch : ts_tag) buf_.push_back(ch);
        for (int i = 0; i < read_n; ++i) buf_.push_back(ts_buf[i]);
        for (int i = 0; i < end_n; ++i) buf_.push_back(end_buf[i]);
        return true;
      }
    }
    return true;
  } else {
    if (ts_match_ > 0) {
      for (size_t i = 0; i < ts_match_; ++i) buf_.push_back(ts_tag[i]);
      ts_match_ = (c == ts_tag[0]) ? 1 : 0;
      if (ts_match_ > 0) return true;
    }
  }

  buf_.push_back(c);

  // 2. Check for </title>
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
