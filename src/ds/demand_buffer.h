#ifndef DEMAND_BUFFER_H_
#define DEMAND_BUFFER_H_

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace demand_mem {

struct Region {
  void* base = nullptr;
  size_t reserved_bytes = 0;
  std::vector<uint64_t> committed;
};

void Reserve(Region& r, size_t min_bytes);
void CommitPages(Region& r, size_t page_lo, size_t page_hi);
void Release(Region& r);

}  // namespace demand_mem

// Virtual-memory-backed array that reserves address space up front without
// charging OS commit/working set and demand-commits 4 KiB pages on first use.
template <typename T>
class DemandBuffer {
 public:
  static constexpr size_t kPageShift = 12;
  static constexpr size_t kPageSize = size_t(1) << kPageShift;

  DemandBuffer() = default;
  explicit DemandBuffer(size_t count) { Init(count); }
  ~DemandBuffer() { Release(); }

  DemandBuffer(const DemandBuffer&) = delete;
  DemandBuffer& operator=(const DemandBuffer&) = delete;

  DemandBuffer(DemandBuffer&& o) noexcept
      : data_(o.data_), count_(o.count_), region_(std::move(o.region_)) {
    o.data_ = nullptr;
    o.count_ = 0;
    o.region_.base = nullptr;
    o.region_.reserved_bytes = 0;
  }

  DemandBuffer& operator=(DemandBuffer&& o) noexcept {
    if (this != &o) {
      Release();
      data_ = o.data_;
      count_ = o.count_;
      region_ = std::move(o.region_);
      o.data_ = nullptr;
      o.count_ = 0;
      o.region_.base = nullptr;
      o.region_.reserved_bytes = 0;
    }
    return *this;
  }

  void Init(size_t count) {
    Release();
    count_ = count;
    if (count == 0) return;
    demand_mem::Reserve(region_, count * sizeof(T));
    data_ = static_cast<T*>(region_.base);
  }

  void Release() {
    demand_mem::Release(region_);
    data_ = nullptr;
    count_ = 0;
  }

  size_t size() const { return count_; }
  size_t reserved_bytes() const { return region_.reserved_bytes; }

  inline bool IsPageCommitted(size_t page_idx) const {
    return ((region_.committed[page_idx >> 6] >> (page_idx & 63)) & 1ULL) != 0;
  }

  inline void EnsureElement(size_t idx) {
    const size_t byte_lo = idx * sizeof(T);
    const size_t byte_hi = byte_lo + sizeof(T) - 1;
    const size_t p_lo = byte_lo >> kPageShift;
    const size_t p_hi = byte_hi >> kPageShift;
    if (p_lo == p_hi && IsPageCommitted(p_lo)) return;
    demand_mem::CommitPages(region_, p_lo, p_hi);
  }

  inline T& operator[](size_t idx) { return data_[idx]; }
  inline const T& operator[](size_t idx) const { return data_[idx]; }

 private:
  T* data_ = nullptr;
  size_t count_ = 0;
  demand_mem::Region region_;
};

#endif  // DEMAND_BUFFER_H_
