#ifndef METADATA_H_
#define METADATA_H_

#include <stdio.h>
#include <deque>
#include <string>

namespace preprocessor {
class Dictionary;
}

namespace metadata {

enum TransformFlags {
  TRANSFORM_NONE = 0,
  TRANSFORM_PAGE_IDS = 1,
  TRANSFORM_TIMESTAMPS = 2,
  TRANSFORM_BOILERPLATE = 4,
  TRANSFORM_ALL = (TRANSFORM_PAGE_IDS | TRANSFORM_TIMESTAMPS | TRANSFORM_BOILERPLATE)
};

// Transforms Wikipedia Metadata (Header compaction, Page-ID deltas, compact timestamps, and boilerplate tags).
// Writes the transformed text stream to `out`.
// Returns true if any metadata was transformed; false if no Wikipedia metadata found.
bool Transform(FILE* in, int len, FILE* out, int flags = TRANSFORM_ALL);

// Streaming decoder that wraps a stream source (Dictionary or FILE)
// and dynamically expands header, delta Page-IDs, compact timestamps, and boilerplate tags back to canonical text.
class StreamingDecoder {
 public:
  StreamingDecoder(preprocessor::Dictionary* dict, FILE* in, int flags = TRANSFORM_ALL);

  int GetByte();

 private:
  bool Fill();

  preprocessor::Dictionary* dict_;
  FILE* in_;
  int flags_;
  unsigned long long prev_id_;
  bool first_byte_;
  std::deque<int> buf_;
};

}  // namespace metadata

#endif  // METADATA_H_
