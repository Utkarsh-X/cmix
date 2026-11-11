#include "demand_buffer.h"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace demand_mem {

namespace {
constexpr size_t kPageSize = 4096;

inline size_t AlignPageUp(size_t bytes) {
  if (bytes > SIZE_MAX - (kPageSize - 1)) {
    std::fprintf(stderr, "DemandBuffer size overflow (%zu bytes)\n", bytes);
    std::abort();
  }
  return (bytes + (kPageSize - 1)) & ~(kPageSize - 1);
}
}  // namespace

void Reserve(Region& r, size_t min_bytes) {
  if (min_bytes == 0) return;
  const size_t aligned = AlignPageUp(min_bytes);
  const size_t num_pages = aligned >> 12;
#ifdef _WIN32
  void* p = VirtualAlloc(nullptr, aligned, MEM_RESERVE, PAGE_NOACCESS);
  if (!p) {
    std::fprintf(stderr,
                 "DemandBuffer VirtualAlloc MEM_RESERVE failed (%zu bytes, err=%lu)\n",
                 aligned, GetLastError());
    std::abort();
  }
  r.base = p;
  r.reserved_bytes = aligned;
  r.committed.assign((num_pages + 63) >> 6, 0ULL);
#else
  void* p = mmap(nullptr, aligned, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) {
    std::perror("DemandBuffer mmap");
    std::abort();
  }
  r.base = p;
  r.reserved_bytes = aligned;
  r.committed.assign((num_pages + 63) >> 6, ~0ULL);
#endif
}

void CommitPages(Region& r, size_t page_lo, size_t page_hi) {
#ifdef _WIN32
  size_t p = page_lo;
  while (p <= page_hi) {
    while (p <= page_hi &&
           ((r.committed[p >> 6] >> (p & 63)) & 1ULL) != 0) {
      ++p;
    }
    if (p > page_hi) break;
    const size_t start = p;
    while (p <= page_hi &&
           ((r.committed[p >> 6] >> (p & 63)) & 1ULL) == 0) {
      ++p;
    }
    const size_t count = p - start;
    void* addr = static_cast<char*>(r.base) + (start << 12);
    if (!VirtualAlloc(addr, count << 12, MEM_COMMIT, PAGE_READWRITE)) {
      std::fprintf(stderr,
                   "DemandBuffer VirtualAlloc MEM_COMMIT failed (%zu pages, err=%lu)\n",
                   count, GetLastError());
      std::abort();
    }
    for (size_t q = start; q < p; ++q) {
      r.committed[q >> 6] |= (1ULL << (q & 63));
    }
  }
#else
  (void)r;
  (void)page_lo;
  (void)page_hi;
#endif
}

void Release(Region& r) {
  if (r.base) {
#ifdef _WIN32
    VirtualFree(r.base, 0, MEM_RELEASE);
#else
    munmap(r.base, r.reserved_bytes);
#endif
    r.base = nullptr;
  }
  r.reserved_bytes = 0;
  r.committed.clear();
}

}  // namespace demand_mem
