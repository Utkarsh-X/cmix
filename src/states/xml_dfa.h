#ifndef XML_DFA_H_
#define XML_DFA_H_

#include <cstdint>
#include <algorithm>

class XmlDfa {
 public:
  enum TargetType : uint8_t {
    TARGET_NONE = 0,
    TARGET_EXACT_BYTE = 1,
    TARGET_DIGIT_0_9 = 2,
    TARGET_DIGIT_0_1 = 3, // month tens: '0'..'1'
    TARGET_DIGIT_0_3 = 4, // day tens: '0'..'3'
    TARGET_DIGIT_0_2 = 5, // hour tens: '0'..'2'
    TARGET_DIGIT_0_5 = 6  // min/sec tens: '0'..'5'
  };

  enum State : uint8_t {
    STATE_IDLE = 0,
    STATE_TAG_START = 1,        // seen '<'
    STATE_TAG_CLOSE_SLASH = 2,  // seen '</'
    STATE_TAG_OPEN_T = 3,       // seen '<t'
    STATE_TAG_OPEN_TI = 4,      // seen '<ti'
    STATE_TAG_CLOSE_T = 5,      // seen '</t'
    STATE_TAG_CLOSE_TI = 6,     // seen '</ti'
    STATE_TAG_OPEN_C = 7,       // seen '<c'
    STATE_TAG_OPEN_CO = 8,      // seen '<co'
    STATE_TAG_CLOSE_C = 9,      // seen '</c'
    STATE_TAG_CLOSE_CO = 10,    // seen '</co'
    STATE_SEQ = 11,             // matching deterministic sequence
    STATE_TIMESTAMP = 12        // in 20-char ISO 8601 timestamp
  };

  XmlDfa() {
    InitShiftTable();
  }

  void Reset() {
    state_ = STATE_IDLE;
    target_type_ = TARGET_NONE;
    expected_byte_ = 0;
    seq_str_ = nullptr;
    seq_pos_ = 0;
    seq_len_ = 0;
    ts_pos_ = 0;
  }

  // Returns:
  //   1: bit is deterministically 1 (clamp to 0.99995)
  //   0: bit is deterministically 0 (clamp to 0.00005)
  //  -1: bit has entropy or mismatch (use standard CMIX prediction)
  inline int PredictBit(unsigned int bit_context) const {
    if (target_type_ == TARGET_NONE || bit_context == 0 || bit_context >= 256) {
      return -1;
    }

    if (target_type_ == TARGET_EXACT_BYTE) {
      const uint8_t shift = kShift[bit_context];
      const uint32_t expected_node = (256u + expected_byte_) >> (shift + 1);
      if (bit_context == expected_node) {
        return ((256u + expected_byte_) >> shift) & 1;
      }
      return -1; // Mismatch with expected byte prefix
    }

    // Digit targets: upper 4 bits are 0011 (0x30)
    if (bit_context == 1) return 0;
    if (bit_context == 2) return 0;
    if (bit_context == 4) return 1;
    if (bit_context == 9) return 1;

    switch (target_type_) {
      case TARGET_DIGIT_0_9:
        if (bit_context == 39) return 0; // digits 8,9: bit 2 is 0
        if (bit_context == 78) return 0; // digits 8,9: bit 1 is 0
        return -1;

      case TARGET_DIGIT_0_1:
        if (bit_context == 19) return 0;
        if (bit_context == 38) return 0;
        if (bit_context == 76) return 0;
        return -1; // bit 0 can be '0' or '1'

      case TARGET_DIGIT_0_3:
        if (bit_context == 19) return 0;
        if (bit_context == 38) return 0;
        return -1;

      case TARGET_DIGIT_0_2:
        if (bit_context == 19) return 0;
        if (bit_context == 38) return 0;
        if (bit_context == 153) return 0; // for '2', bit 0 is 0
        return -1;

      case TARGET_DIGIT_0_5:
        if (bit_context == 19) return 0;
        if (bit_context == 77) return 0; // digits 4,5: bit 1 is 0
        return -1;

      default:
        return -1;
    }
  }

  // Called on byte boundaries (manager_.bit_context_ >= 128)
  void Update(uint8_t byte) {
    switch (state_) {
      case STATE_IDLE:
        if (byte == '<') {
          state_ = STATE_TAG_START;
        }
        break;

      case STATE_TAG_START:
        if (byte == '/') {
          state_ = STATE_TAG_CLOSE_SLASH;
        } else if (byte == 'p') {
          SetSeq("age>", 4, STATE_IDLE);
        } else if (byte == 'r') {
          SetSeq("evision>", 8, STATE_IDLE);
        } else if (byte == 'i') {
          SetSeq("d>", 2, STATE_IDLE);
        } else if (byte == 'u') {
          SetSeq("sername>", 8, STATE_IDLE);
        } else if (byte == 'm') {
          SetSeq("inor />", 7, STATE_IDLE);
        } else if (byte == 't') {
          state_ = STATE_TAG_OPEN_T;
        } else if (byte == 'c') {
          state_ = STATE_TAG_OPEN_C;
        } else {
          ResetToIdle(byte);
        }
        break;

      case STATE_TAG_CLOSE_SLASH:
        if (byte == 'p') {
          SetSeq("age>", 4, STATE_IDLE);
        } else if (byte == 'r') {
          SetSeq("evision>", 8, STATE_IDLE);
        } else if (byte == 'i') {
          SetSeq("d>", 2, STATE_IDLE);
        } else if (byte == 'u') {
          SetSeq("sername>", 8, STATE_IDLE);
        } else if (byte == 't') {
          state_ = STATE_TAG_CLOSE_T;
        } else if (byte == 'c') {
          state_ = STATE_TAG_CLOSE_C;
        } else {
          ResetToIdle(byte);
        }
        break;

      case STATE_TAG_OPEN_T:
        if (byte == 'i') {
          state_ = STATE_TAG_OPEN_TI;
        } else if (byte == 'e') {
          SetSeq("xt", 2, STATE_IDLE);
        } else {
          ResetToIdle(byte);
        }
        break;

      case STATE_TAG_OPEN_TI:
        if (byte == 't') {
          SetSeq("le>", 3, STATE_IDLE);
        } else if (byte == 'm') {
          // Matched "<tim". Next is "estamp>" followed by timestamp body!
          SetSeq("estamp>", 7, STATE_TIMESTAMP);
        } else {
          ResetToIdle(byte);
        }
        break;

      case STATE_TAG_CLOSE_T:
        if (byte == 'i') {
          state_ = STATE_TAG_CLOSE_TI;
        } else if (byte == 'e') {
          SetSeq("xt>", 3, STATE_IDLE);
        } else {
          ResetToIdle(byte);
        }
        break;

      case STATE_TAG_CLOSE_TI:
        if (byte == 't') {
          SetSeq("le>", 3, STATE_IDLE);
        } else if (byte == 'm') {
          SetSeq("estamp>", 7, STATE_IDLE);
        } else {
          ResetToIdle(byte);
        }
        break;

      case STATE_TAG_OPEN_C:
        if (byte == 'o') state_ = STATE_TAG_OPEN_CO;
        else ResetToIdle(byte);
        break;

      case STATE_TAG_OPEN_CO:
        if (byte == 'n') SetSeq("tributor>", 9, STATE_IDLE);
        else if (byte == 'm') SetSeq("ment>", 5, STATE_IDLE);
        else ResetToIdle(byte);
        break;

      case STATE_TAG_CLOSE_C:
        if (byte == 'o') state_ = STATE_TAG_CLOSE_CO;
        else ResetToIdle(byte);
        break;

      case STATE_TAG_CLOSE_CO:
        if (byte == 'n') SetSeq("tributor>", 9, STATE_IDLE);
        else if (byte == 'm') SetSeq("ment>", 5, STATE_IDLE);
        else ResetToIdle(byte);
        break;

      case STATE_SEQ:
        if (byte == static_cast<uint8_t>(seq_str_[seq_pos_])) {
          ++seq_pos_;
          if (seq_pos_ >= seq_len_) {
            if (seq_next_state_ == STATE_TIMESTAMP) {
              state_ = STATE_TIMESTAMP;
              ts_pos_ = 0;
              SetTimestampTarget(0);
            } else {
              state_ = STATE_IDLE;
              target_type_ = TARGET_NONE;
            }
          } else {
            target_type_ = TARGET_EXACT_BYTE;
            expected_byte_ = static_cast<uint8_t>(seq_str_[seq_pos_]);
          }
        } else {
          ResetToIdle(byte);
        }
        break;

      case STATE_TIMESTAMP:
        if (ValidateTimestampByte(ts_pos_, byte)) {
          ++ts_pos_;
          if (ts_pos_ == 20) {
            // ISO 8601 complete ('Z' seen). Transition directly into closing tag </timestamp>
            SetSeq("</timestamp>", 12, STATE_IDLE);
          } else {
            SetTimestampTarget(ts_pos_);
          }
        } else {
          ResetToIdle(byte);
        }
        break;
    }
  }

 private:
  void SetSeq(const char* str, uint8_t len, State next_state) {
    state_ = STATE_SEQ;
    seq_str_ = str;
    seq_len_ = len;
    seq_pos_ = 0;
    seq_next_state_ = next_state;
    target_type_ = TARGET_EXACT_BYTE;
    expected_byte_ = static_cast<uint8_t>(seq_str_[0]);
  }

  void SetTimestampTarget(uint8_t pos) {
    switch (pos) {
      case 0: target_type_ = TARGET_EXACT_BYTE; expected_byte_ = '2'; break;
      case 1: target_type_ = TARGET_EXACT_BYTE; expected_byte_ = '0'; break;
      case 2: target_type_ = TARGET_EXACT_BYTE; expected_byte_ = '0'; break;
      case 3: target_type_ = TARGET_DIGIT_0_9; break;
      case 4: target_type_ = TARGET_EXACT_BYTE; expected_byte_ = '-'; break;
      case 5: target_type_ = TARGET_DIGIT_0_1; break;
      case 6: target_type_ = TARGET_DIGIT_0_9; break;
      case 7: target_type_ = TARGET_EXACT_BYTE; expected_byte_ = '-'; break;
      case 8: target_type_ = TARGET_DIGIT_0_3; break;
      case 9: target_type_ = TARGET_DIGIT_0_9; break;
      case 10: target_type_ = TARGET_EXACT_BYTE; expected_byte_ = 'T'; break;
      case 11: target_type_ = TARGET_DIGIT_0_2; break;
      case 12: target_type_ = TARGET_DIGIT_0_9; break;
      case 13: target_type_ = TARGET_EXACT_BYTE; expected_byte_ = ':'; break;
      case 14: target_type_ = TARGET_DIGIT_0_5; break;
      case 15: target_type_ = TARGET_DIGIT_0_9; break;
      case 16: target_type_ = TARGET_EXACT_BYTE; expected_byte_ = ':'; break;
      case 17: target_type_ = TARGET_DIGIT_0_5; break;
      case 18: target_type_ = TARGET_DIGIT_0_9; break;
      case 19: target_type_ = TARGET_EXACT_BYTE; expected_byte_ = 'Z'; break;
      default: target_type_ = TARGET_NONE; break;
    }
  }

  bool ValidateTimestampByte(uint8_t pos, uint8_t byte) const {
    switch (pos) {
      case 0: return byte == '2';
      case 1: return byte == '0';
      case 2: return byte == '0';
      case 3: return byte >= '0' && byte <= '9';
      case 4: return byte == '-';
      case 5: return byte == '0' || byte == '1';
      case 6: return byte >= '0' && byte <= '9';
      case 7: return byte == '-';
      case 8: return byte >= '0' && byte <= '3';
      case 9: return byte >= '0' && byte <= '9';
      case 10: return byte == 'T';
      case 11: return byte >= '0' && byte <= '2';
      case 12: return byte >= '0' && byte <= '9';
      case 13: return byte == ':';
      case 14: return byte >= '0' && byte <= '5';
      case 15: return byte >= '0' && byte <= '9';
      case 16: return byte == ':';
      case 17: return byte >= '0' && byte <= '5';
      case 18: return byte >= '0' && byte <= '9';
      case 19: return byte == 'Z';
      default: return false;
    }
  }

  void ResetToIdle(uint8_t byte) {
    state_ = (byte == '<') ? STATE_TAG_START : STATE_IDLE;
    target_type_ = TARGET_NONE;
    expected_byte_ = 0;
    seq_str_ = nullptr;
    seq_pos_ = 0;
    seq_len_ = 0;
    ts_pos_ = 0;
  }

  static void InitShiftTable() {
    static bool initialized = false;
    if (!initialized) {
      for (int node = 1; node < 256; ++node) {
        int depth = 0;
        while ((node >> (depth + 1)) != 0) ++depth;
        kShift[node] = static_cast<uint8_t>(7 - depth);
      }
      initialized = true;
    }
  }

  State state_ = STATE_IDLE;
  TargetType target_type_ = TARGET_NONE;
  uint8_t expected_byte_ = 0;
  const char* seq_str_ = nullptr;
  uint8_t seq_pos_ = 0;
  uint8_t seq_len_ = 0;
  State seq_next_state_ = STATE_IDLE;
  uint8_t ts_pos_ = 0;

  static inline uint8_t kShift[256];
};

#endif // XML_DFA_H_
