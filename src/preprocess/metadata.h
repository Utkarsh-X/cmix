#ifndef METADATA_H_
#define METADATA_H_

#include <stdio.h>
#include <deque>
#include <string>
#include <map>

namespace preprocessor {
class Dictionary;
}

namespace metadata {

enum TransformFlags {
  TRANSFORM_NONE = 0,
  TRANSFORM_PAGE_IDS = 1,
  TRANSFORM_TIMESTAMPS = 2,
  TRANSFORM_BOILERPLATE = 4,
  TRANSFORM_REV_IDS = 8,
  TRANSFORM_ALL = (TRANSFORM_PAGE_IDS | TRANSFORM_TIMESTAMPS | TRANSFORM_BOILERPLATE | TRANSFORM_REV_IDS)
};

// Transforms Wikipedia Metadata (Header compaction, Page-ID deltas, B1 Rev-ID forecast, compact timestamps, and boilerplate tags).
// Writes the transformed text stream to `out`.
// Returns true if any metadata was transformed; false if no Wikipedia metadata found.
bool Transform(FILE* in, int len, FILE* out, int flags = TRANSFORM_ALL);

// Streaming decoder that wraps a stream source (Dictionary or FILE)
// and dynamically expands header, delta Page-IDs, B1 Rev-IDs, compact timestamps, and boilerplate tags back to canonical text.
class StreamingDecoder {
 public:
  StreamingDecoder(preprocessor::Dictionary* dict, FILE* in, int flags = TRANSFORM_ALL);

  int GetByte();

 private:
  bool Fill();
  bool StartsWith(const std::string& prefix) const;
  bool SubstrEquals(size_t offset, const std::string& str) const;
  void Consume(size_t n);
  void PushString(const std::string& s);

  preprocessor::Dictionary* dict_;
  FILE* in_;
  int flags_;
  unsigned long long prev_id_;
  unsigned long long last_b1_pid_;
  unsigned long long last_b1_rid_;
  std::map<unsigned long long, unsigned long long> b1_map_;
  bool first_byte_;
  bool eof_;
  std::deque<char> window_;
  std::deque<int> buf_;
};

}  // namespace metadata

#endif  // METADATA_H_
