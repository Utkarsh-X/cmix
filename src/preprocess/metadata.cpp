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

  // 2. XML Boilerplate & Inter-page transition (EXP-035, EXP-038, EXP-039)
  if (flags & TRANSFORM_BOILERPLATE) {
    ReplaceAll(s, "<text>", "<text_lit>");
    // EXP-039: Strip \n      indentation before <text xml:space="preserve">
    ReplaceAll(s, "\n      <text xml:space=\"preserve\">", "<text>");
    ReplaceAll(s, "<text xml:space=\"preserve\">", "<text_fallback>");

    ReplaceAll(s, "</text>\n    </revision>\n  </page>\n  <page>\n    <title>", "</text>\n<page><title>");

    // EXP-038: Contributor & Revision Schema Compaction
    ReplaceAll(s, "\n    <revision>\n      <id>", "\n<revision><id>");
    ReplaceAll(s, "\n      <contributor>\n        <username>", "\n<username>");
    ReplaceAll(s, "</username>\n        <id>", "</username><id>");
    ReplaceAll(s, "</id>\n      </contributor>", "</id></contributor>");
    ReplaceAll(s, "\n      <contributor>\n        <ip>", "\n<ip>");
    ReplaceAll(s, "</ip>\n      </contributor>", "</ip></contributor>");
    ReplaceAll(s, "\n      <minor />", "\n<minor>");
    ReplaceAll(s, "\n      <comment>", "\n<comment>");

    // EXP-039: Strip \n      indentation before <timestamp>
    ReplaceAll(s, "</id>\n      <timestamp>", "</id><timestamp>");
    ++count_bp;
  }

  // 3. Timestamp Normalization (EXP-033)
  if (flags & TRANSFORM_TIMESTAMPS) {
    size_t pos = 0;
    while ((pos = s.find("<timestamp>", pos)) != std::string::npos) {
      if (pos + 31 < s.size() && s.substr(pos + 31, 12) == "</timestamp>") {
        std::string ts = s.substr(pos + 11, 20);
        if (ts[0] == '2' && ts[1] == '0' && ts[4] == '-' && ts[7] == '-' &&
            ts[10] == 'T' && ts[13] == ':' && ts[16] == ':' && ts[19] == 'Z') {
          std::string compact;
          compact += ts[2]; compact += ts[3];   // YY
          compact += ts[5]; compact += ts[6];   // MM
          compact += ts[8]; compact += ts[9];   // DD
          compact += ts[11]; compact += ts[12]; // HH
          compact += ts[14]; compact += ts[15]; // MM
          compact += ts[17]; compact += ts[18]; // SS
          s.replace(pos + 11, 20, compact);
          ++count_ts;
        }
      }
      pos += 11;
    }
  }

  // 4. Page-ID Delta Encoding (EXP-032, EXP-039) & B1 Revision-ID Forecast (EXP-040)
  if (flags & TRANSFORM_PAGE_IDS) {
    size_t pos = 0;
    unsigned long long prev_id = 0;
    unsigned long long last_b1_pid = 0;
    unsigned long long last_b1_rid = 0;

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

          // EXP-040: B1 Revision-ID Compaction (< 17,000,000)
          if (flags & TRANSFORM_REV_IDS) {
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
                }
              }
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
    : dict_(dict), in_(in), flags_(flags), prev_id_(0), last_b1_pid_(0), last_b1_rid_(0), first_byte_(true), eof_(false) {}

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

      PushString("\n    <revision>\n      <id>");
      PushString(std::to_string(revid));
      Consume(end_tag);
      return true;
    }
  }

  // 3b. Standard revision tag
  if ((flags_ & TRANSFORM_BOILERPLATE) && StartsWith("\n<revision><id>")) {
    Consume(15);
    PushString("\n    <revision>\n      <id>");
    return true;
  }

  // 4a. Compact Timestamp (EXP-039: </id><timestamp>YYMMDDHHMMSS</timestamp>, 40 chars)
  if ((flags_ & TRANSFORM_TIMESTAMPS) && StartsWith("</id><timestamp>") && window_.size() >= 40 && SubstrEquals(28, "</timestamp>")) {
    bool is_num = true;
    for (size_t i = 16; i < 28; ++i) {
      if (window_[i] < '0' || window_[i] > '9') { is_num = false; break; }
    }
    if (is_num) {
      std::string ts = "</id>\n      <timestamp>20";
      ts += window_[16]; ts += window_[17]; ts += '-';
      ts += window_[18]; ts += window_[19]; ts += '-';
      ts += window_[20]; ts += window_[21]; ts += 'T';
      ts += window_[22]; ts += window_[23]; ts += ':';
      ts += window_[24]; ts += window_[25]; ts += ':';
      ts += window_[26]; ts += window_[27]; ts += "Z</timestamp>";
      Consume(40);
      PushString(ts);
      return true;
    }
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
