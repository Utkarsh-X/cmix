#ifndef DIRECT_STORAGE_H_
#define DIRECT_STORAGE_H_

#include "../ds/demand_buffer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Shared lazy-initialized, demand-committed storage for Direct and DirectHash.
// Untouched rows remain uncommitted (0 PrivateCommit, 0 WorkingSet) and return
// the exact baseline initial prediction of 0.5f until first Perceive().
class LazyDirectTable {
 public:
  explicit LazyDirectTable(size_t size)
      : size_(size),
        predictions_(size),
        counts_(size),
        ready_((size + 63) / 64, 0ULL) {}

  size_t size() const { return size_; }

  inline bool IsReady(size_t row) const {
    return ((ready_[row >> 6] >> (row & 63)) & 1ULL) != 0;
  }

  inline float Predict(size_t row, unsigned int bit_context) const {
    return IsReady(row) ? predictions_[row][bit_context] : 0.5f;
  }

  inline void Perceive(size_t row, unsigned int bit_context, int bit,
                       int limit, float delta, float divisor) {
    if (!IsReady(row)) InitRow(row);
    float d = divisor;
    if (counts_[row][bit_context] < limit) {
      ++counts_[row][bit_context];
      d = 1.0 / (counts_[row][bit_context] + delta);
    }
    predictions_[row][bit_context] +=
        (bit - predictions_[row][bit_context]) * d;
  }

  inline void ResetIfReady(size_t row) {
    if (IsReady(row)) {
      predictions_[row].fill(0.5);
      counts_[row].fill(0);
    }
  }

 private:
  void InitRow(size_t row) {
    predictions_.EnsureElement(row);
    counts_.EnsureElement(row);
    predictions_[row].fill(0.5);
    // counts_[row] is already zero-filled by DemandBuffer fresh page commit.
    ready_[row >> 6] |= (1ULL << (row & 63));
  }

  size_t size_;
  DemandBuffer<std::array<float, 256>> predictions_;
  DemandBuffer<std::array<unsigned char, 256>> counts_;
  std::vector<uint64_t> ready_;
};

#endif  // DIRECT_STORAGE_H_
