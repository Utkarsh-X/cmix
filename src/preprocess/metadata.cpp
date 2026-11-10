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
  int count_id = 0, count_ts = 0, count_bp = 0;

  while (pos < len && (c = getc(in)) != EOF) {
    ++pos;

    if (c == '<') {
      std::string tag_buf;
      tag_buf += '<';
      while (pos < len && tag_buf.size() < 40 && (c = getc(in)) != EOF) {
        ++pos;
        tag_buf += static_cast<char>(c);
        if (c == '>') break;
      }

      // 1. Check for <timestamp>
      if ((flags & TRANSFORM_TIMESTAMPS) && tag_buf == "<timestamp>") {
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

      // 2. Check for <text xml:space="preserve">
      if ((flags & TRANSFORM_BOILERPLATE) && tag_buf == "<text xml:space=\"preserve\">") {
        fputs("<text>", out);
        ++count_bp;
        continue;
      }
      if ((flags & TRANSFORM_BOILERPLATE) && tag_buf == "<text>") {
        fputs("<text_lit>", out);
        continue;
      }

      // 3. Check for </text> followed by page boundary
      if ((flags & TRANSFORM_BOILERPLATE) && tag_buf == "</text>") {
        const std::string boundary = "\n    </revision>\n  </page>\n  <page>\n    <title>";
        char b_buf[55];
        int bn = 0;
        while (bn < (int)boundary.size() && pos < len && (c = getc(in)) != EOF) {
          ++pos;
          b_buf[bn++] = static_cast<char>(c);
        }
        if (bn == (int)boundary.size() && strncmp(b_buf, boundary.c_str(), boundary.size()) == 0) {
          fputs("</text>\n<page><title>", out);
          ++count_bp;
          continue;
        } else {
          const std::string comp = "\n<page><title>";
          if (bn >= (int)comp.size() && strncmp(b_buf, comp.c_str(), comp.size()) == 0) {
            fputs("</text>\n<page_lit><title>", out);
            for (int i = (int)comp.size(); i < bn; ++i) putc(b_buf[i], out);
            continue;
          }
          fputs("</text>", out);
          for (int i = 0; i < bn; ++i) putc(b_buf[i], out);
          continue;
        }
      }

      // 4. Check for </title>
      if ((flags & TRANSFORM_PAGE_IDS) && tag_buf == "</title>") {
        fputs("</title>", out);
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

      // None of the tags matched, emit buffered tag_buf
      fputs(tag_buf.c_str(), out);
      continue;
    }

    putc(c, out);
  }
  return (count_id > 0 || count_ts > 0 || count_bp > 0);
}

StreamingDecoder::StreamingDecoder(preprocessor::Dictionary* dict, FILE* in, int flags)
    : dict_(dict), in_(in), flags_(flags), prev_id_(0) {}

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

  if (c == '<') {
    std::string tag_buf;
    tag_buf += '<';
    while (tag_buf.size() < 40 && (c = next_byte()) != EOF) {
      tag_buf += static_cast<char>(c);
      if (c == '>') break;
    }

    // 1. Check for <timestamp>
    if ((flags_ & TRANSFORM_TIMESTAMPS) && tag_buf == "<timestamp>") {
      char ts_buf[12];
      int read_n = 0;
      while (read_n < 12 && (c = next_byte()) != EOF) {
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
        while (end_n < 12 && (c = next_byte()) != EOF) {
          end_buf[end_n++] = static_cast<char>(c);
        }
      }
      if (read_n == 12 && end_n == 12 && strncmp(end_buf, "</timestamp>", 12) == 0) {
        const std::string open_ts = "<timestamp>";
        for (char ch : open_ts) buf_.push_back(ch);
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
        const std::string open_ts = "<timestamp>";
        for (char ch : open_ts) buf_.push_back(ch);
        for (int i = 0; i < read_n; ++i) buf_.push_back(ts_buf[i]);
        for (int i = 0; i < end_n; ++i) buf_.push_back(end_buf[i]);
        return true;
      }
    }

    // 2. Check for <text> or <text_lit>
    if ((flags_ & TRANSFORM_BOILERPLATE) && tag_buf == "<text>") {
      const std::string expanded = "<text xml:space=\"preserve\">";
      for (char ch : expanded) buf_.push_back(ch);
      return true;
    }
    if ((flags_ & TRANSFORM_BOILERPLATE) && tag_buf == "<text_lit>") {
      const std::string lit = "<text>";
      for (char ch : lit) buf_.push_back(ch);
      return true;
    }

    // 3. Check for </text>
    if ((flags_ & TRANSFORM_BOILERPLATE) && tag_buf == "</text>") {
      const std::string close_text = "</text>";
      for (char ch : close_text) buf_.push_back(ch);
      
      const std::string page_prefix = "\n<page";
      char p_buf[6];
      int pn = 0;
      while (pn < 6 && (c = next_byte()) != EOF) {
        p_buf[pn++] = static_cast<char>(c);
      }
      if (pn == 6 && strncmp(p_buf, page_prefix.c_str(), 6) == 0) {
        c = next_byte();
        if (c == '>') {
          char t_buf[7];
          int tn = 0;
          while (tn < 7 && (c = next_byte()) != EOF) {
            t_buf[tn++] = static_cast<char>(c);
          }
          if (tn == 7 && strncmp(t_buf, "<title>", 7) == 0) {
            const std::string boundary = "\n    </revision>\n  </page>\n  <page>\n    <title>";
            for (char ch : boundary) buf_.push_back(ch);
            return true;
          } else {
            for (char ch : page_prefix) buf_.push_back(ch);
            buf_.push_back('>');
            for (int i = 0; i < tn; ++i) buf_.push_back(t_buf[i]);
            return true;
          }
        } else if (c == '_') {
          char l_buf[11];
          int ln = 0;
          while (ln < 11 && (c = next_byte()) != EOF) {
            l_buf[ln++] = static_cast<char>(c);
          }
          if (ln == 11 && strncmp(l_buf, "lit><title>", 11) == 0) {
            const std::string comp = "\n<page><title>";
            for (char ch : comp) buf_.push_back(ch);
            return true;
          } else {
            for (char ch : page_prefix) buf_.push_back(ch);
            buf_.push_back('_');
            for (int i = 0; i < ln; ++i) buf_.push_back(l_buf[i]);
            return true;
          }
        } else {
          for (char ch : page_prefix) buf_.push_back(ch);
          if (c != EOF) buf_.push_back(c);
          return true;
        }
      } else {
        for (int i = 0; i < pn; ++i) buf_.push_back(p_buf[i]);
        return true;
      }
    }

    // 4. Check for </title>
    if ((flags_ & TRANSFORM_PAGE_IDS) && tag_buf == "</title>") {
      const std::string title_tag = "</title>";
      for (char ch : title_tag) buf_.push_back(ch);
      std::vector<int> ws;
      while ((c = next_byte()) != EOF) {
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
          while ((c = next_byte()) != EOF) {
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

    // No match, push all buffered characters
    for (char ch : tag_buf) buf_.push_back(ch);
    return true;
  }

  buf_.push_back(c);
  return true;
}

}  // namespace metadata
