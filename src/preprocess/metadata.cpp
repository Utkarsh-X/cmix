#include "metadata.h"
#include "dictionary.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace metadata {

const size_t kMediaWikiHeaderLen = 1406;
const char* kMediaWikiHeader = "<mediawiki xmlns=\"http://www.mediawiki.org/xml/export-0.3/\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:schemaLocation=\"http://www.mediawiki.org/xml/export-0.3/ http://www.mediawiki.org/xml/export-0.3.xsd\" version=\"0.3\" xml:lang=\"en\">\n  <siteinfo>\n    <sitename>Wikipedia</sitename>\n    <base>http://en.wikipedia.org/wiki/Main_Page</base>\n    <generator>MediaWiki 1.6alpha</generator>\n    <case>first-letter</case>\n      <namespaces>\n      <namespace key=\"-2\">Media</namespace>\n      <namespace key=\"-1\">Special</namespace>\n      <namespace key=\"0\" />\n      <namespace key=\"1\">Talk</namespace>\n      <namespace key=\"2\">User</namespace>\n      <namespace key=\"3\">User talk</namespace>\n      <namespace key=\"4\">Wikipedia</namespace>\n      <namespace key=\"5\">Wikipedia talk</namespace>\n      <namespace key=\"6\">Image</namespace>\n      <namespace key=\"7\">Image talk</namespace>\n      <namespace key=\"8\">MediaWiki</namespace>\n      <namespace key=\"9\">MediaWiki talk</namespace>\n      <namespace key=\"10\">Template</namespace>\n      <namespace key=\"11\">Template talk</namespace>\n      <namespace key=\"12\">Help</namespace>\n      <namespace key=\"13\">Help talk</namespace>\n      <namespace key=\"14\">Category</namespace>\n      <namespace key=\"15\">Category talk</namespace>\n      <namespace key=\"100\">Portal</namespace>\n      <namespace key=\"101\">Portal talk</namespace>\n    </namespaces>\n  </siteinfo>\n  ";

const char* kCompactHeader = "<mediawiki>\n  ";
const size_t kCompactHeaderLen = 14;

namespace {

void ReplaceAll(std::string& str, const std::string& from, const std::string& to) {
  if (from.empty()) return;
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
}

}  // namespace

bool Transform(FILE* in, int len, FILE* out, int flags) {
  std::string s;
  s.resize(len);
  size_t n_read = fread(&s[0], 1, len, in);
  s.resize(n_read);

  int count_id = 0, count_ts = 0, count_bp = 0, count_rev = 0;

  // 1. Header compaction (EXP-036)
  if ((flags & TRANSFORM_BOILERPLATE) && s.size() >= kMediaWikiHeaderLen &&
      memcmp(s.data(), kMediaWikiHeader, kMediaWikiHeaderLen) == 0) {
    s.replace(0, kMediaWikiHeaderLen, kCompactHeader);
    ++count_bp;
  }

  // 2. XML Boilerplate & Inter-page transition (EXP-186 Token-Safe)
  if (flags & TRANSFORM_BOILERPLATE) {
    ReplaceAll(s, "<text>", "<text_lit>");
    ReplaceAll(s, "\n      <text xml:space=\"preserve\">", "<text>");
    ReplaceAll(s, "<text xml:space=\"preserve\">", "<text_fallback>");

    // Token-Safe Inter-page transition
    ReplaceAll(s, "</text>\n    </revision>\n  </page>\n  <page>\n    <title>", "\n<page><title>");

    // Contributor & Revision Schema Compaction
    ReplaceAll(s, "\n    <revision>\n      <id>", "\n<revision><id>");
    ReplaceAll(s, "\n      <contributor>\n        <username>", "<username>");
    ReplaceAll(s, "</username>\n        <id>", "</username><id>");
    ReplaceAll(s, "</id>\n      </contributor>", "</uid>");
    ReplaceAll(s, "\n      <contributor>\n        <ip>", "<ip>");
    ReplaceAll(s, "</ip>\n      </contributor>", "</uip>");
    ReplaceAll(s, "\n      <minor />", "<minor>");
    ReplaceAll(s, "\n      <comment>", "<comment>");

    ReplaceAll(s, "</id>\n      <timestamp>", "</id><timestamp>");
    ++count_bp;
  }

  // 3. E1 Revision ID + E3b Timestamp Forecasting
  if ((flags & TRANSFORM_PAGE_IDS) || (flags & TRANSFORM_TIMESTAMPS) || (flags & TRANSFORM_REV_IDS)) {
    size_t pos = 0;
    unsigned long long prev_id = 0;
    B1Interp enc_b1;
    B2Basis enc_b2;
    long long prev_sec = 0;
    int ts_count = 0;

    while ((pos = s.find("</title>", pos)) != std::string::npos) {
      size_t ws_start = pos + 8;
      size_t id_pos = ws_start;
      while (id_pos < s.size() && (s[id_pos] == ' ' || s[id_pos] == '\t' || s[id_pos] == '\r' || s[id_pos] == '\n')) {
        ++id_pos;
      }
      if (id_pos + 4 < s.size() && s.substr(id_pos, 4) == "<id>") {
        size_t num_start = id_pos + 4;
        size_t num_end = num_start;
        while (num_end < s.size() && s[num_end] >= '0' && s[num_end] <= '9') ++num_end;
        if (num_end + 5 <= s.size() && s.substr(num_end, 5) == "</id>") {
          unsigned long long curr_id = std::stoull(s.substr(num_start, num_end - num_start));
          unsigned long long delta = curr_id - prev_id;
          prev_id = curr_id;

          if (flags & TRANSFORM_PAGE_IDS) {
            std::string repl = "<id>" + std::to_string(delta) + "</id>";
            s.replace(ws_start, (num_end + 5) - ws_start, repl);
            pos = ws_start + repl.size();
            ++count_id;
          } else {
            pos = num_end + 5;
          }

          // Process Revision ID and Timestamp
          size_t next_page = s.find("<page>", pos);
          size_t rev_pos = s.find("\n<revision><id>", pos);
          if (rev_pos != std::string::npos && (next_page == std::string::npos || rev_pos < next_page)) {
            size_t rid_start = rev_pos + 15;
            size_t rid_end = rid_start;
            while (rid_end < s.size() && s[rid_end] >= '0' && s[rid_end] <= '9') ++rid_end;
            if (rid_end + 5 <= s.size() && s.substr(rid_end, 5) == "</id>") {
              unsigned long long revid = std::stoull(s.substr(rid_start, rid_end - rid_start));

              // E3b Timestamp Encoding
              if (flags & TRANSFORM_TIMESTAMPS) {
                size_t ts_pos = s.find("</id><timestamp>", rid_end);
                if (ts_pos == rid_end) {
                  size_t ts_val_start = ts_pos + 16;
                  size_t ts_end = s.find("</timestamp>", ts_val_start);
                  if (ts_end != std::string::npos && ts_end == ts_val_start + 20) {
                    std::string ts = s.substr(ts_val_start, 20);
                    if (ts[4] == '-' && ts[7] == '-' && ts[10] == 'T' && ts[13] == ':' && ts[16] == ':' && ts[19] == 'Z') {
                      long long y = atoi(ts.c_str());
                      unsigned m = atoi(ts.c_str() + 5);
                      unsigned d = atoi(ts.c_str() + 8);
                      unsigned hh = atoi(ts.c_str() + 11);
                      unsigned mm = atoi(ts.c_str() + 14);
                      unsigned ss = atoi(ts.c_str() + 17);
                      long long sec = TsToSeconds(y, m, d, hh, mm, ss);

                      long long pred = 0;
                      std::string res_str;
                      if (enc_b2.Prognose(revid, &pred)) {
                        long long res = sec - pred;
                        if (res == 0) res_str = "=";
                        else if (res > 0) res_str = "+" + std::to_string(res);
                        else res_str = std::to_string(res);
                      } else {
                        if (ts_count == 0) {
                          // Safe caret '^' prefix avoiding dictionary '@' 0x0C escape
                          res_str = "^" + std::to_string(sec);
                        } else {
                          long long res = sec - prev_sec;
                          if (res == 0) res_str = "=";
                          else if (res > 0) res_str = "+" + std::to_string(res);
                          else res_str = std::to_string(res);
                        }
                      }
                      enc_b2.Beobachte(revid, sec);
                      prev_sec = sec;
                      ++ts_count;
                      ++count_ts;

                      s.replace(ts_val_start, 20, res_str);
                    }
                  }
                }
              }

              // E1 Revision ID Encoding (revid < 17,000,000)
              if (flags & TRANSFORM_REV_IDS) {
                if (revid < B1Interp::kB1Max) {
                  long long forecast = enc_b1.Prognose(curr_id);
                  long long res = static_cast<long long>(revid) - forecast;
                  enc_b1.Add(curr_id, revid);

                  std::string rev_repl;
                  if (res == 0) rev_repl = "+";
                  else rev_repl = "+" + std::to_string(res);

                  s.replace(rid_start, rid_end - rid_start, rev_repl);
                  pos = rid_start + rev_repl.size();
                  ++count_rev;
                } else {
                  pos = rid_end;
                }
              } else {
                pos = rid_end;
              }
              continue;
            }
          }
        }
      }
      pos += 8;
    }
  }

  fwrite(s.data(), 1, s.size(), out);
  return (count_id > 0 || count_ts > 0 || count_bp > 0 || count_rev > 0);
}

StreamingDecoder::StreamingDecoder(preprocessor::Dictionary* dict, FILE* in, int flags)
    : dict_(dict), in_(in), flags_(flags), prev_id_(0), curr_revid_(0),
      dec_prev_sec_(0), first_byte_(true), eof_(false) {}

int StreamingDecoder::GetByte() {
  while (buf_.empty()) {
    if (!Fill()) return -1;
  }
  int c = buf_.front();
  buf_.pop_front();
  return c;
}

bool StreamingDecoder::StartsWith(const char* prefix) const {
  size_t len = strlen(prefix);
  if (window_.size() < len) return false;
  for (size_t i = 0; i < len; ++i) {
    if (window_[i] != prefix[i]) return false;
  }
  return true;
}

bool StreamingDecoder::SubstrEquals(size_t offset, const char* str) const {
  size_t len = strlen(str);
  if (offset + len > window_.size()) return false;
  for (size_t i = 0; i < len; ++i) {
    if (window_[offset + i] != str[i]) return false;
  }
  return true;
}

void StreamingDecoder::Consume(size_t n) {
  for (size_t i = 0; i < n; ++i) window_.pop_front();
}

void StreamingDecoder::PushString(const std::string& s) {
  for (char ch : s) buf_.push_back(static_cast<unsigned char>(ch));
}

bool StreamingDecoder::Fill() {
  auto next_byte = [this]() -> int {
    if (dict_ != NULL) return dict_->Decode(in_);
    return getc(in_);
  };

  while (window_.size() < 128 && !eof_) {
    int c = next_byte();
    if (c == EOF) {
      eof_ = true;
      break;
    }
    window_.push_back(static_cast<char>(c));
  }
  if (window_.empty()) return false;

  // 1. MediaWiki header expansion
  if (first_byte_) {
    first_byte_ = false;
    if ((flags_ & TRANSFORM_BOILERPLATE) && StartsWith(kCompactHeader)) {
      Consume(kCompactHeaderLen);
      PushString(kMediaWikiHeader);
      return true;
    }
  }

  // 2. Inter-page transition: \n<page><title> (14 chars)
  if ((flags_ & TRANSFORM_BOILERPLATE) && StartsWith("\n<page><title>")) {
    Consume(14);
    PushString("</text>\n    </revision>\n  </page>\n  <page>\n    <title>");
    return true;
  }

  // 3. Contributor closing tokens
  if (flags_ & TRANSFORM_BOILERPLATE) {
    if (StartsWith("</uid>")) {
      Consume(6);
      PushString("</id>\n      </contributor>");
      return true;
    }
    if (StartsWith("</uip>")) {
      Consume(6);
      PushString("</ip>\n      </contributor>");
      return true;
    }
  }

  // 4. Page-ID Delta expansion
  if ((flags_ & TRANSFORM_PAGE_IDS) && StartsWith("</title><id>")) {
    size_t num_start = 12;
    size_t num_end = num_start;
    while (num_end < window_.size() && window_[num_end] >= '0' && window_[num_end] <= '9') ++num_end;
    if (num_end + 5 <= window_.size() && SubstrEquals(num_end, "</id>")) {
      std::string delta_str;
      for (size_t i = num_start; i < num_end; ++i) delta_str += window_[i];
      unsigned long long delta = std::stoull(delta_str);
      unsigned long long curr_id = prev_id_ + delta;
      prev_id_ = curr_id;

      PushString("</title>\n    <id>");
      PushString(std::to_string(curr_id));
      PushString("</id>");
      Consume(num_end + 5);
      return true;
    }
    if (num_end + 5 > window_.size() && !eof_) return false;
  }

  // 5a. E1 Revision ID expansion: \n<revision><id>+...
  if ((flags_ & TRANSFORM_REV_IDS) && StartsWith("\n<revision><id>+")) {
    size_t end_tag = 16;
    while (end_tag < window_.size() && window_[end_tag] != '<') ++end_tag;
    if (end_tag + 5 <= window_.size() && SubstrEquals(end_tag, "</id>")) {
      long long res = 0;
      if (end_tag > 16) {
        std::string res_str;
        for (size_t i = 16; i < end_tag; ++i) res_str += window_[i];
        res = std::stoll(res_str);
      }
      long long forecast = dec_b1_.Prognose(prev_id_);
      unsigned long long revid = static_cast<unsigned long long>(forecast + res);
      dec_b1_.Add(prev_id_, revid);
      curr_revid_ = revid;

      PushString("\n    <revision>\n      <id>");
      PushString(std::to_string(revid));
      Consume(end_tag);
      return true;
    }
    if (end_tag + 5 > window_.size() && !eof_) return false;
  }

  // 5b. Standard Revision ID
  if (StartsWith("\n<revision><id>")) {
    size_t num_start = 15;
    size_t num_end = num_start;
    while (num_end < window_.size() && window_[num_end] >= '0' && window_[num_end] <= '9') ++num_end;
    if (num_end > num_start && num_end + 5 <= window_.size() && SubstrEquals(num_end, "</id>")) {
      std::string id_str;
      for (size_t i = num_start; i < num_end; ++i) id_str += window_[i];
      unsigned long long revid = std::stoull(id_str);
      curr_revid_ = revid;

      PushString("\n    <revision>\n      <id>");
      PushString(id_str);
      Consume(num_end);
      return true;
    }
    if (num_end + 5 > window_.size() && !eof_) return false;
  }

  // 6. E3b Timestamp after </id>
  if ((flags_ & TRANSFORM_TIMESTAMPS) && StartsWith("</id><timestamp>")) {
    size_t end_tag = 16;
    while (end_tag < window_.size() && window_[end_tag] != '<') ++end_tag;
    if (end_tag + 12 <= window_.size() && SubstrEquals(end_tag, "</timestamp>")) {
      std::string token;
      for (size_t i = 16; i < end_tag; ++i) token += window_[i];

      long long sec = 0;
      long long pred = 0;
      if (token[0] == '^') {
        sec = std::stoll(token.substr(1));
      } else if (dec_b2_.Prognose(curr_revid_, &pred)) {
        if (token == "=") sec = pred;
        else sec = pred + std::stoll(token);
      } else {
        if (token == "=") sec = dec_prev_sec_;
        else sec = dec_prev_sec_ + std::stoll(token);
      }

      dec_b2_.Beobachte(curr_revid_, sec);
      dec_prev_sec_ = sec;

      long long y; unsigned m, d, hh, mm, ss;
      SecondsToTs(sec, &y, &m, &d, &hh, &mm, &ss);
      char ts_buf[32];
      snprintf(ts_buf, sizeof(ts_buf), "%04lld-%02u-%02uT%02u:%02u:%02uZ", y, m, d, hh, mm, ss);

      PushString("</id>\n      <timestamp>");
      PushString(ts_buf);
      PushString("</timestamp>");
      Consume(end_tag + 12);
      return true;
    }
    if (end_tag + 12 > window_.size() && !eof_) return false;
  }

  // 7. Boilerplate & Schema tokens
  if (flags_ & TRANSFORM_BOILERPLATE) {
    if (StartsWith("<text>")) {
      Consume(6);
      PushString("\n      <text xml:space=\"preserve\">");
      return true;
    }
    if (StartsWith("<text_lit>")) {
      Consume(10);
      PushString("<text>");
      return true;
    }
    if (StartsWith("<text_fallback>")) {
      Consume(15);
      PushString("<text xml:space=\"preserve\">");
      return true;
    }
    if (StartsWith("<username>")) {
      Consume(10);
      PushString("\n      <contributor>\n        <username>");
      return true;
    }
    if (StartsWith("</username><id>")) {
      Consume(15);
      PushString("</username>\n        <id>");
      return true;
    }
    if (StartsWith("<ip>")) {
      Consume(4);
      PushString("\n      <contributor>\n        <ip>");
      return true;
    }
    if (StartsWith("<minor>")) {
      Consume(7);
      PushString("\n      <minor />");
      return true;
    }
    if (StartsWith("<comment>")) {
      Consume(9);
      PushString("\n      <comment>");
      return true;
    }
  }

  // Default: emit byte
  buf_.push_back(static_cast<unsigned char>(window_.front()));
  window_.pop_front();
  return true;
}

}  // namespace metadata
