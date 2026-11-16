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

    // EXP-186: Token-Safe Inter-page transition
    ReplaceAll(s, "</text>\n    </revision>\n  </page>\n  <page>\n    <title>", "\n<page><title>");

    // EXP-038 & EXP-186: Contributor & Revision Schema Compaction
    ReplaceAll(s, "\n    <revision>\n      <id>", "\n<revision><id>");
    // EXP-045: Eliminate intra-schema boundary newlines
    ReplaceAll(s, "\n      <contributor>\n        <username>", "<username>");
    ReplaceAll(s, "</username>\n        <id>", "</username><id>");
    ReplaceAll(s, "</id>\n      </contributor>", "</uid>");
    ReplaceAll(s, "\n      <contributor>\n        <ip>", "<ip>");
    ReplaceAll(s, "</ip>\n      </contributor>", "</uip>");
    ReplaceAll(s, "\n      <minor />", "<minor>");
    ReplaceAll(s, "\n      <comment>", "<comment>");

    // EXP-186: Token-Safe Timestamp Compaction
    ReplaceAll(s, "</id>\n      <timestamp>", "</id><timestamp>");
    ++count_bp;
  }

  // 3. Timestamp Normalization & Token Compaction (EXP-033 & EXP-186)
  if (flags & TRANSFORM_TIMESTAMPS) {
    size_t pos = 0;
    while ((pos = s.find("<timestamp>", pos)) != std::string::npos) {
      if (pos + 31 < s.size() && s.substr(pos + 31, 12) == "</timestamp>") {
        std::string ts = s.substr(pos + 11, 20);
        if (ts[0] == '2' && ts[1] == '0' && ts[4] == '-' && ts[7] == '-' &&
            ts[10] == 'T' && ts[13] == ':' && ts[16] == ':' && ts[19] == 'Z') {
          std::string compact = "<ts>";
          compact += ts[2]; compact += ts[3];   // YY
          compact += ts[5]; compact += ts[6];   // MM
          compact += ts[8]; compact += ts[9];   // DD
          compact += ts[11]; compact += ts[12]; // HH
          compact += ts[14]; compact += ts[15]; // MM
          compact += ts[17]; compact += ts[18]; // SS
          s.replace(pos, 43, compact);
          pos += compact.size();
          ++count_ts;
          continue;
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

  // 5. EXP-188: Piped Wikilink Compaction
  size_t count_wl = 0;
  if (flags & TRANSFORM_WIKILINKS) {
    std::string out_s;
    out_s.reserve(s.size());
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
      if (i + 2 <= n && s[i] == '[' && s[i + 1] == '[') {
        size_t end_b = s.find("]]", i + 2);
        size_t next_o = s.find("[[", i + 2);
        size_t next_nl = s.find('\n', i + 2);
        if (end_b != std::string::npos &&
            (next_o == std::string::npos || next_o > end_b) &&
            (next_nl == std::string::npos || next_nl > end_b)) {
          std::string inside = s.substr(i + 2, end_b - (i + 2));
          size_t pipe = inside.find('|');
          if (pipe != std::string::npos && inside.find('|', pipe + 1) == std::string::npos) {
            std::string target = inside.substr(0, pipe);
            std::string anchor = inside.substr(pipe + 1);
            std::string repl = "";
            bool matched = false;

            if (target == anchor) {
              repl = "+"; matched = true;
            } else {
              size_t fsp = target.find(' ');
              size_t lsp = target.rfind(' ');
              if (lsp != std::string::npos && target.substr(lsp + 1) == anchor) {
                repl = "~"; matched = true;
              } else if (fsp != std::string::npos && target.substr(0, fsp) == anchor) {
                repl = "^"; matched = true;
              } else if (anchor.size() > target.size() && anchor.compare(0, target.size(), target) == 0) {
                repl = "+" + anchor.substr(target.size()); matched = true;
              } else if (target.size() > 2 && target.back() == ')') {
                size_t paren = target.rfind(" (");
                if (paren != std::string::npos && target.substr(0, paren) == anchor) {
                  repl = "%"; matched = true;
                }
              }
            }

            if (matched) {
              out_s += "[[" + target + "|" + repl + "]]";
              i = end_b + 2;
              ++count_wl;
              continue;
            } else if (!anchor.empty() && (anchor[0] == '~' || anchor[0] == '^' || anchor[0] == '%' || anchor[0] == '+')) {
              out_s += "[[" + target + "|" + anchor[0] + anchor + "]]";
              i = end_b + 2;
              continue;
            }
          }
        }
      }
      out_s += s[i++];
    }
    s = std::move(out_s);
  }

  fwrite(s.data(), 1, s.size(), out);
  return (count_id > 0 || count_ts > 0 || count_bp > 0 || count_rev > 0 || count_wl > 0);
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

  while (window_.size() < 512 && !eof_) {
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

  // 4a. Token-Safe Timestamp after </id>: </id><ts>YYMMDDHHMMSS (21 chars)
  if ((flags_ & TRANSFORM_TIMESTAMPS) && StartsWith("</id><ts>") && window_.size() >= 21) {
    bool is_num = true;
    for (size_t i = 9; i < 21; ++i) {
      if (window_[i] < '0' || window_[i] > '9') { is_num = false; break; }
    }
    if (is_num) {
      std::string ts = "</id>\n      <timestamp>20";
      ts += window_[9]; ts += window_[10]; ts += '-';
      ts += window_[11]; ts += window_[12]; ts += '-';
      ts += window_[13]; ts += window_[14]; ts += 'T';
      ts += window_[15]; ts += window_[16]; ts += ':';
      ts += window_[17]; ts += window_[18]; ts += ':';
      ts += window_[19]; ts += window_[20]; ts += "Z</timestamp>";
      Consume(21);
      PushString(ts);
      return true;
    }
  }

  // 4b. Token-Safe Fallback Timestamp: <ts>YYMMDDHHMMSS (16 chars)
  if ((flags_ & TRANSFORM_TIMESTAMPS) && StartsWith("<ts>") && window_.size() >= 16) {
    bool is_num = true;
    for (size_t i = 4; i < 16; ++i) {
      if (window_[i] < '0' || window_[i] > '9') { is_num = false; break; }
    }
    if (is_num) {
      std::string ts = "<timestamp>20";
      ts += window_[4]; ts += window_[5]; ts += '-';
      ts += window_[6]; ts += window_[7]; ts += '-';
      ts += window_[8]; ts += window_[9]; ts += 'T';
      ts += window_[10]; ts += window_[11]; ts += ':';
      ts += window_[12]; ts += window_[13]; ts += ':';
      ts += window_[14]; ts += window_[15]; ts += "Z</timestamp>";
      Consume(16);
      PushString(ts);
      return true;
    }
  }

  // 4c. Legacy Compact Timestamp (EXP-039: </id><timestamp>YYMMDDHHMMSS</timestamp>, 40 chars)
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

  // 4d. Legacy Fallback Timestamp (35 chars: <timestamp>YYMMDDHHMMSS</timestamp>)
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
    // EXP-186: Inter-page transition: \n<page><title> (14 chars)
    if (StartsWith("\n<page><title>")) {
      Consume(14);
      PushString("</text>\n    </revision>\n  </page>\n  <page>\n    <title>");
      return true;
    }
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

    // EXP-186: Contributor closing tokens
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

  // 8. EXP-188: Piped Wikilink Expansion
  if ((flags_ & TRANSFORM_WIKILINKS) && window_.size() >= 2 && window_[0] == '[' && window_[1] == '[') {
    size_t end_b = std::string::npos;
    bool invalid = false;
    for (size_t i = 2; i + 1 < window_.size(); ++i) {
      if (window_[i] == '\n' || (window_[i] == '[' && window_[i + 1] == '[')) {
        invalid = true; break;
      }
      if (window_[i] == ']' && window_[i + 1] == ']') {
        end_b = i; break;
      }
    }
    if (!invalid && end_b != std::string::npos) {
      std::string inside(window_.begin() + 2, window_.begin() + end_b);
      size_t pipe = inside.find('|');
      if (pipe != std::string::npos && inside.find('|', pipe + 1) == std::string::npos) {
        std::string target = inside.substr(0, pipe);
        std::string anchor = inside.substr(pipe + 1);
        std::string orig = "";
        bool matched = false;

        if (anchor.size() >= 2 && (anchor[0] == '~' || anchor[0] == '^' || anchor[0] == '%' || anchor[0] == '+') && anchor[1] == anchor[0]) {
          orig = anchor.substr(1); matched = true;
        } else if (anchor == "~") {
          size_t sp = target.rfind(' ');
          if (sp != std::string::npos) { orig = target.substr(sp + 1); matched = true; }
        } else if (anchor == "^") {
          size_t sp = target.find(' ');
          if (sp != std::string::npos) { orig = target.substr(0, sp); matched = true; }
        } else if (anchor == "%") {
          size_t p = target.rfind(" (");
          if (p != std::string::npos) { orig = target.substr(0, p); matched = true; }
        } else if (anchor == "+") {
          orig = target; matched = true;
        } else if (anchor.size() >= 2 && anchor[0] == '+') {
          orig = target + anchor.substr(1); matched = true;
        }

        if (matched) {
          Consume(end_b + 2);
          PushString("[[" + target + "|" + orig + "]]");
          return true;
        }
      }
    } else if (!invalid && !eof_) {
      return false; // Wait for further bytes in sliding window
    }
  }

  // Default: emit byte
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
