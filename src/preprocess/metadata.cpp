#include "metadata.h"
#include "dictionary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <deque>

namespace metadata {

const size_t kMediaWikiHeaderLen = 1406;
const char* kMediaWikiHeader = "<mediawiki xmlns=\"http://www.mediawiki.org/xml/export-0.3/\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:schemaLocation=\"http://www.mediawiki.org/xml/export-0.3/ http://www.mediawiki.org/xml/export-0.3.xsd\" version=\"0.3\" xml:lang=\"en\">\n  <siteinfo>\n    <sitename>Wikipedia</sitename>\n    <base>http://en.wikipedia.org/wiki/Main_Page</base>\n    <generator>MediaWiki 1.6alpha</generator>\n    <case>first-letter</case>\n      <namespaces>\n      <namespace key=\"-2\">Media</namespace>\n      <namespace key=\"-1\">Special</namespace>\n      <namespace key=\"0\" />\n      <namespace key=\"1\">Talk</namespace>\n      <namespace key=\"2\">User</namespace>\n      <namespace key=\"3\">User talk</namespace>\n      <namespace key=\"4\">Wikipedia</namespace>\n      <namespace key=\"5\">Wikipedia talk</namespace>\n      <namespace key=\"6\">Image</namespace>\n      <namespace key=\"7\">Image talk</namespace>\n      <namespace key=\"8\">MediaWiki</namespace>\n      <namespace key=\"9\">MediaWiki talk</namespace>\n      <namespace key=\"10\">Template</namespace>\n      <namespace key=\"11\">Template talk</namespace>\n      <namespace key=\"12\">Help</namespace>\n      <namespace key=\"13\">Help talk</namespace>\n      <namespace key=\"14\">Category</namespace>\n      <namespace key=\"15\">Category talk</namespace>\n      <namespace key=\"100\">Portal</namespace>\n      <namespace key=\"101\">Portal talk</namespace>\n    </namespaces>\n  </siteinfo>\n  ";

const char* kCompactHeader = "<mediawiki>\n  ";
const size_t kCompactHeaderLen = 14;

bool Transform(FILE* in, int len, FILE* out, int flags) {
  int pos = 0;
  int c;
  int count_id = 0, count_ts = 0, count_bp = 0;
  unsigned long long prev_id = 0;

  // 1. Check if stream begins with MediaWiki export header
  if (flags & TRANSFORM_BOILERPLATE) {
    char h_buf[kMediaWikiHeaderLen + 1];
    size_t h_read = fread(h_buf, 1, kMediaWikiHeaderLen, in);
    pos += (int)h_read;

    if (h_read == kMediaWikiHeaderLen && memcmp(h_buf, kMediaWikiHeader, kMediaWikiHeaderLen) == 0) {
      fputs(kCompactHeader, out);
      ++count_bp;
    } else {
      for (size_t i = 0; i < h_read; ++i) putc(h_buf[i], out);
    }
  }

  // 2. Stream through remaining content (EXP-035 foundation)
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

      fputs(tag_buf.c_str(), out);
      continue;
    }

    putc(c, out);
  }
  return (count_id > 0 || count_ts > 0 || count_bp > 0);
}

StreamingDecoder::StreamingDecoder(preprocessor::Dictionary* dict, FILE* in, int flags)
    : dict_(dict), in_(in), flags_(flags), prev_id_(0), first_byte_(true) {}

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

  // Header expansion at start of stream
  if (first_byte_) {
    first_byte_ = false;
    if (flags_ & TRANSFORM_BOILERPLATE) {
      char h_buf[15];
      size_t hn = 0;
      int c;
      while (hn < 14 && (c = next_byte()) != EOF) {
        h_buf[hn++] = static_cast<char>(c);
      }
      if (hn == 14 && memcmp(h_buf, "<mediawiki>\n  ", 14) == 0) {
        for (size_t i = 0; i < kMediaWikiHeaderLen; ++i) buf_.push_back(kMediaWikiHeader[i]);
        return true;
      } else {
        for (size_t i = 0; i < hn; ++i) buf_.push_back(h_buf[i]);
        if (hn == 0) return false;
        return true;
      }
    }
  }

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
