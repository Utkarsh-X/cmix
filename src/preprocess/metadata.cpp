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

static inline long long Fdiv(long long a, long long b) {
  long long q = a / b, r = a % b;
  if (r != 0 && ((r < 0) != (b < 0))) --q;
  return q;
}

static inline long long DaysFromCivil(long long y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097LL + (long long)doe - 719468LL;
}

static inline void CivilFromDays(long long z, long long* y, unsigned* m, unsigned* d) {
  z += 719468LL;
  const long long era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long long yy = (long long)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp + (mp < 10 ? 3 : -9);
  *y = yy + (*m <= 2);
}

static inline long long TsToSeconds(long long y, unsigned m, unsigned d, unsigned hh, unsigned mm, unsigned ss) {
  long long days = DaysFromCivil(y, m, d);
  return days * 86400LL + hh * 3600LL + mm * 60LL + ss;
}

static inline void SecondsToTs(long long sec, long long* y, unsigned* m, unsigned* d, unsigned* hh, unsigned* mm, unsigned* ss) {
  long long days = Fdiv(sec, 86400LL);
  long long rem = sec - days * 86400LL;
  *hh = (unsigned)(rem / 3600LL);
  rem %= 3600LL;
  *mm = (unsigned)(rem / 60LL);
  *ss = (unsigned)(rem % 60LL);
  CivilFromDays(days, y, m, d);
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

  // 2. XML Boilerplate & Inter-page transition (EXP-035, EXP-038, EXP-039)
  if (flags & TRANSFORM_BOILERPLATE) {
    ReplaceAll(s, "<text>", "<text_lit>");
    // EXP-039: Strip \n      indentation before <text xml:space="preserve">
    ReplaceAll(s, "\n      <text xml:space=\"preserve\">", "<text>");
    ReplaceAll(s, "<text xml:space=\"preserve\">", "<text_fallback>");

    ReplaceAll(s, "</text>\n    </revision>\n  </page>\n  <page>\n    <title>", "</text>\n<page><title>");

    // EXP-038: Contributor & Revision Schema Compaction
    ReplaceAll(s, "\n    <revision>\n      <id>", "\n<revision><id>");
    // EXP-045: Eliminate intra-schema boundary newlines
    ReplaceAll(s, "\n      <contributor>\n        <username>", "<username>");
    ReplaceAll(s, "</username>\n        <id>", "</username><id>");
    ReplaceAll(s, "</id>\n      </contributor>", "</id></contributor>");
    ReplaceAll(s, "\n      <contributor>\n        <ip>", "<ip>");
    ReplaceAll(s, "</ip>\n      </contributor>", "</ip></contributor>");
    ReplaceAll(s, "\n      <minor />", "<minor>");
    ReplaceAll(s, "\n      <comment>", "<comment>");

    // EXP-039: Strip \n      indentation before <timestamp>
    ReplaceAll(s, "</id>\n      <timestamp>", "</id><timestamp>");
    ++count_bp;
  }

  // 3 & 4. Page-ID Delta (EXP-032, EXP-039), B1 Revision-ID Forecast (EXP-040), and EXP-080 B2 Timestamp Compaction
  if (flags & TRANSFORM_PAGE_IDS) {
    size_t pos = 0;
    unsigned long long prev_id = 0;
    unsigned long long last_b1_pid = 0;
    unsigned long long last_b1_rid = 0;

    B2Basis enc_b2;
    long long prev_sec = 0;
    unsigned long long prev_revid = 0;
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
        while (num_end < s.size() && s[num_end] >= '0' && s[num_end] <= '9') {
          ++num_end;
        }
        if (num_end + 5 <= s.size() && s.substr(num_end, 5) == "</id>") {
          unsigned long long curr_id = std::stoull(s.substr(num_start, num_end - num_start));
          unsigned long long delta = curr_id - prev_id;
          prev_id = curr_id;
          std::string repl = "<id>" + std::to_string(delta) + "</id>";
          s.replace(ws_start, (num_end + 5) - ws_start, repl);
          pos = ws_start + repl.size();
          ++count_id;

          size_t next_page = s.find("<page>", pos);
          size_t rev_pos = s.find("\n<revision><id>", pos);
          if (rev_pos != std::string::npos && (next_page == std::string::npos || rev_pos < next_page)) {
            size_t rid_start = rev_pos + 15;
            size_t rid_end = rid_start;
            while (rid_end < s.size() && s[rid_end] >= '0' && s[rid_end] <= '9') {
              ++rid_end;
            }
            if (rid_end + 5 <= s.size() && s.substr(rid_end, 5) == "</id>") {
              unsigned long long revid = std::stoull(s.substr(rid_start, rid_end - rid_start));

              // EXP-080: Monotonic Timestamp Extrapolation & Differential Compaction
              if (flags & TRANSFORM_TIMESTAMPS) {
                size_t ts_pos = s.find("</id><timestamp>", rid_end);
                if (ts_pos == rid_end) {
                  size_t ts_val_start = ts_pos + 16;
                  size_t ts_end = s.find("</timestamp>", ts_val_start);
                  if (ts_end != std::string::npos && ts_end == ts_val_start + 20) {
                    std::string ts = s.substr(ts_val_start, 20);
                    if (ts[0] == '2' && ts[1] == '0' && ts[4] == '-' && ts[7] == '-' &&
                        ts[10] == 'T' && ts[13] == ':' && ts[16] == ':' && ts[19] == 'Z') {
                      long long y = atoi(ts.c_str());
                      unsigned m = atoi(ts.c_str() + 5);
                      unsigned d = atoi(ts.c_str() + 8);
                      unsigned hh = atoi(ts.c_str() + 11);
                      unsigned mm = atoi(ts.c_str() + 14);
                      unsigned ss = atoi(ts.c_str() + 17);
                      long long sec = TsToSeconds(y, m, d, hh, mm, ss);

                      std::string ts_repl;
                      if (ts_count == 0) {
                        ts_repl += ts[2]; ts_repl += ts[3];
                        ts_repl += ts[5]; ts_repl += ts[6];
                        ts_repl += ts[8]; ts_repl += ts[9];
                        ts_repl += ts[11]; ts_repl += ts[12];
                        ts_repl += ts[14]; ts_repl += ts[15];
                        ts_repl += ts[17]; ts_repl += ts[18];
                      } else {
                        long long dt = sec - prev_sec;
                        long long pred = 0;
                        bool can_b2 = (revid > prev_revid) && (revid - prev_revid <= 1000000ULL) && enc_b2.Prognose(revid, &pred);
                        long long res_b2 = can_b2 ? (sec - pred) : 999999999LL;

                        // Condition 1: Small residual fallback (|dt| <= 300s)
                        if (std::abs(dt) <= 300) {
                          if (dt == 0) ts_repl = ":=";
                          else if (dt > 0) ts_repl = ":+" + std::to_string(dt);
                          else ts_repl = ":" + std::to_string(dt);
                        }
                        // Condition 2: Monotonic B2 extrapolation with small residual (|res_b2| <= 300s)
                        else if (can_b2 && std::abs(res_b2) <= 300) {
                          if (res_b2 == 0) ts_repl = "=";
                          else if (res_b2 > 0) ts_repl = "+" + std::to_string(res_b2);
                          else ts_repl = std::to_string(res_b2);
                        }
                        // Condition 3: Fallback to literal compact 12-char
                        else {
                          ts_repl += ts[2]; ts_repl += ts[3];
                          ts_repl += ts[5]; ts_repl += ts[6];
                          ts_repl += ts[8]; ts_repl += ts[9];
                          ts_repl += ts[11]; ts_repl += ts[12];
                          ts_repl += ts[14]; ts_repl += ts[15];
                          ts_repl += ts[17]; ts_repl += ts[18];
                        }
                      }

                      enc_b2.Beobachte(revid, sec);
                      prev_sec = sec;
                      prev_revid = revid;
                      ++ts_count;
                      ++count_ts;

                      s.replace(ts_val_start, 20, ts_repl);
                    }
                  }
                }
              }

              // EXP-040: B1 Revision-ID Compaction (< 17,000,000)
              if (flags & TRANSFORM_REV_IDS) {
                if (revid < 17000000ULL) {
                  unsigned long long forecast = (last_b1_pid == 0) ? 15898943ULL : (last_b1_rid + (curr_id - last_b1_pid));
                  long long res = static_cast<long long>(revid) - static_cast<long long>(forecast);
                  last_b1_pid = curr_id;
                  last_b1_rid = revid;
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
          continue;
        }
      }
      pos += 8;
    }
  }

  fwrite(s.data(), 1, s.size(), out);
  return (count_id > 0 || count_ts > 0 || count_bp > 0 || count_rev > 0);
}

StreamingDecoder::StreamingDecoder(preprocessor::Dictionary* dict, FILE* in, int flags)
    : dict_(dict), in_(in), flags_(flags), prev_id_(0), last_b1_pid_(0), last_b1_rid_(0),
      curr_revid_(0), dec_prev_sec_(0), first_byte_(true), eof_(false) {}

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

  while (window_.size() < 48 && !eof_) {
    int c = next_byte();
    if (c == EOF) {
      eof_ = true;
      break;
    }
    window_.push_back(static_cast<char>(c));
  }

  if (window_.empty()) return false;

  // 1. Header at stream start
  if (first_byte_) {
    first_byte_ = false;
    if ((flags_ & TRANSFORM_BOILERPLATE) && StartsWith(kCompactHeader)) {
      Consume(kCompactHeaderLen);
      PushString(kMediaWikiHeader);
      return true;
    }
  }

  // 2. Page-ID Delta (EXP-039 compact: </title><id>delta</id>)
  if ((flags_ & TRANSFORM_PAGE_IDS) && StartsWith("</title><id>")) {
    size_t num_start = 12;
    size_t num_end = num_start;
    while (num_end < window_.size() && window_[num_end] >= '0' && window_[num_end] <= '9') {
      ++num_end;
    }
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
  }

  // Fallback for Page-ID Delta with original whitespace (if any)
  if ((flags_ & TRANSFORM_PAGE_IDS) && StartsWith("</title>")) {
    size_t pos = 8;
    while (pos < window_.size() && (window_[pos] == ' ' || window_[pos] == '\t' || window_[pos] == '\r' || window_[pos] == '\n')) {
      ++pos;
    }
    if (pos + 4 <= window_.size() && SubstrEquals(pos, "<id>")) {
      size_t num_start = pos + 4;
      size_t num_end = num_start;
      while (num_end < window_.size() && window_[num_end] >= '0' && window_[num_end] <= '9') {
        ++num_end;
      }
      if (num_end + 5 <= window_.size() && SubstrEquals(num_end, "</id>")) {
        std::string delta_str;
        for (size_t i = num_start; i < num_end; ++i) delta_str += window_[i];
        unsigned long long delta = std::stoull(delta_str);
        unsigned long long curr_id = prev_id_ + delta;
        prev_id_ = curr_id;

        PushString("</title>");
        for (size_t i = 8; i < pos; ++i) buf_.push_back(window_[i]);
        PushString("<id>");
        PushString(std::to_string(curr_id));
        PushString("</id>");

        Consume(num_end + 5);
        return true;
      }
    }
  }

  // 3a. EXP-040: B1 Revision ID (\n<revision><id>+...)
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
      unsigned long long forecast = (last_b1_pid_ == 0) ? 15898943ULL : (last_b1_rid_ + (prev_id_ - last_b1_pid_));
      unsigned long long revid = forecast + res;
      last_b1_pid_ = prev_id_;
      last_b1_rid_ = revid;
      curr_revid_ = revid;

      PushString("\n    <revision>\n      <id>");
      PushString(std::to_string(revid));
      Consume(end_tag);
      return true;
    }
    if (end_tag + 5 > window_.size() && !eof_) return false;
  }

  // 3b. Standard revision tag with literal digits
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

  // 4a. EXP-080 Timestamp after </id> (</id><timestamp>token</timestamp>)
  if ((flags_ & TRANSFORM_TIMESTAMPS) && StartsWith("</id><timestamp>")) {
    size_t end_tag = 16;
    while (end_tag < window_.size() && window_[end_tag] != '<') ++end_tag;
    if (end_tag + 12 <= window_.size() && SubstrEquals(end_tag, "</timestamp>")) {
      std::string token;
      for (size_t i = 16; i < end_tag; ++i) token += window_[i];

      long long sec = 0;
      // Branch 1: Small residual fallback from previous timestamp (: prefix)
      if (token[0] == ':') {
        if (token == ":=") sec = dec_prev_sec_;
        else sec = dec_prev_sec_ + std::stoll(token.substr(1));
      }
      // Branch 2: Monotonic B2 extrapolation residual (+, -, =)
      else if (token[0] == '+' || token[0] == '-' || token[0] == '=') {
        long long pred = 0;
        dec_b2_.Prognose(curr_revid_, &pred);
        if (token == "=") sec = pred;
        else sec = pred + std::stoll(token);
      }
      // Branch 3: Literal 12-char compact timestamp (YYMMDDHHMMSS)
      else {
        long long y = 2000 + (token[0] - '0') * 10 + (token[1] - '0');
        unsigned m = (token[2] - '0') * 10 + (token[3] - '0');
        unsigned d = (token[4] - '0') * 10 + (token[5] - '0');
        unsigned hh = (token[6] - '0') * 10 + (token[7] - '0');
        unsigned mm = (token[8] - '0') * 10 + (token[9] - '0');
        unsigned ss = (token[10] - '0') * 10 + (token[11] - '0');
        sec = TsToSeconds(y, m, d, hh, mm, ss);
      }

      dec_b2_.Beobachte(curr_revid_, sec);
      dec_prev_sec_ = sec;

      long long y; unsigned m, d, hh, mm, ss;
      SecondsToTs(sec, &y, &m, &d, &hh, &mm, &ss);
      char buf[32];
      sprintf(buf, "%04lld-%02u-%02uT%02u:%02u:%02uZ", y, m, d, hh, mm, ss);

      PushString("</id>\n      <timestamp>");
      PushString(buf);
      PushString("</timestamp>");
      Consume(end_tag + 12);
      return true;
    }
    if (end_tag + 12 > window_.size() && !eof_) return false;
  }

  // 4b. Fallback Timestamp (35 chars: <timestamp>YYMMDDHHMMSS</timestamp>)
  if ((flags_ & TRANSFORM_TIMESTAMPS) && StartsWith("<timestamp>") && window_.size() >= 35 && SubstrEquals(23, "</timestamp>")) {
    bool is_num = true;
    for (size_t i = 11; i < 23; ++i) {
      if (window_[i] < '0' || window_[i] > '9') { is_num = false; break; }
    }
    if (is_num) {
      std::string ts = "<timestamp>20";
      ts += window_[11]; ts += window_[12]; ts += '-';
      ts += window_[13]; ts += window_[14]; ts += '-';
      ts += window_[15]; ts += window_[16]; ts += 'T';
      ts += window_[17]; ts += window_[18]; ts += ':';
      ts += window_[19]; ts += window_[20]; ts += ':';
      ts += window_[21]; ts += window_[22]; ts += "Z</timestamp>";
      Consume(35);
      PushString(ts);
      return true;
    }
  }

  // 5. Boilerplate & Schema tokens
  if (flags_ & TRANSFORM_BOILERPLATE) {
    if (StartsWith("</text>\n<page><title>")) {
      Consume(21);
      PushString("</text>\n    </revision>\n  </page>\n  <page>\n    <title>");
      return true;
    }
    // EXP-039: Compact <text> restores \n      <text xml:space="preserve">
    if (StartsWith("<text>")) {
      Consume(6);
      PushString("\n      <text xml:space=\"preserve\">");
      return true;
    }
    if (StartsWith("<text_fallback>")) {
      Consume(15);
      PushString("<text xml:space=\"preserve\">");
      return true;
    }
    if (StartsWith("<text_lit>")) {
      Consume(10);
      PushString("<text>");
      return true;
    }

    if (StartsWith("</id></contributor>")) {
      Consume(19);
      PushString("</id>\n      </contributor>");
      return true;
    }
    if (StartsWith("</ip></contributor>")) {
      Consume(19);
      PushString("</ip>\n      </contributor>");
      return true;
    }
    if (StartsWith("</username><id>")) {
      Consume(15);
      PushString("</username>\n        <id>");
      return true;
    }
    // EXP-045: Compact schema tokens (without leading newline)
    if (StartsWith("<username>")) {
      Consume(10);
      PushString("\n      <contributor>\n        <username>");
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

    // Fallbacks with leading newline
    if (StartsWith("\n<username>")) {
      Consume(11);
      PushString("\n      <contributor>\n        <username>");
      return true;
    }
    if (StartsWith("\n<ip>")) {
      Consume(5);
      PushString("\n      <contributor>\n        <ip>");
      return true;
    }
    if (StartsWith("\n<minor>")) {
      Consume(8);
      PushString("\n      <minor />");
      return true;
    }
    if (StartsWith("\n<comment>")) {
      Consume(10);
      PushString("\n      <comment>");
      return true;
    }
  }

  // Default: pop single byte
  buf_.push_back(static_cast<unsigned char>(window_.front()));
  window_.pop_front();
  return true;
}

bool StreamingDecoder::StartsWith(const std::string& prefix) const {
  if (window_.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (window_[i] != prefix[i]) return false;
  }
  return true;
}

bool StreamingDecoder::SubstrEquals(size_t offset, const std::string& str) const {
  if (window_.size() < offset + str.size()) return false;
  for (size_t i = 0; i < str.size(); ++i) {
    if (window_[offset + i] != str[i]) return false;
  }
  return true;
}

void StreamingDecoder::Consume(size_t n) {
  for (size_t i = 0; i < n && !window_.empty(); ++i) {
    window_.pop_front();
  }
}

void StreamingDecoder::PushString(const std::string& s) {
  for (char ch : s) buf_.push_back(static_cast<unsigned char>(ch));
}

}  // namespace metadata
