#ifndef MSVC_COMPAT_H
#define MSVC_COMPAT_H

#ifdef _MSC_VER

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <immintrin.h>
#include <intrin.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <cmath>
#include <BaseTsd.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

struct u128 {
    uint64_t lo, hi;
    u128() : lo(0), hi(0) {}
    u128(uint64_t v) : lo(v), hi(0) {}
    u128(uint64_t h, uint64_t l) : lo(l), hi(h) {}
    
    operator int() const { return (int)lo; }
    operator unsigned() const { return (unsigned)lo; }
    operator uint64_t() const { return lo; }
    
    u128 operator<<(int shift) const {
        if (shift <= 0) return *this;
        if (shift >= 128) return u128(0, 0);
        if (shift >= 64) return u128(lo << (shift - 64), 0);
        return u128((hi << shift) | (lo >> (64 - shift)), lo << shift);
    }
    u128 operator>>(int shift) const {
        if (shift <= 0) return *this;
        if (shift >= 128) return u128(0, 0);
        if (shift >= 64) return u128(0, hi >> (shift - 64));
        return u128(hi >> shift, (lo >> shift) | (hi << (64 - shift)));
    }
    u128 operator&(const u128& o) const { return u128(hi & o.hi, lo & o.lo); }
    u128 operator|(const u128& o) const { return u128(hi | o.hi, lo | o.lo); }
    u128& operator|=(const u128& o) { hi |= o.hi; lo |= o.lo; return *this; }
    u128& operator&=(const u128& o) { hi &= o.hi; lo &= o.lo; return *this; }
    u128& operator<<=(int shift) { *this = *this << shift; return *this; }
    u128& operator>>=(int shift) { *this = *this >> shift; return *this; }
    u128 operator~() const { return u128(~hi, ~lo); }
    u128& operator+=(const u128& o) {
        uint64_t old_lo = lo;
        lo += o.lo;
        hi += o.hi + (lo < old_lo ? 1 : 0);
        return *this;
    }
    u128 operator+(const u128& o) const {
        u128 res = *this;
        res += o;
        return res;
    }
    bool operator==(const u128& o) const { return lo == o.lo && hi == o.hi; }
};

typedef __m256i __m256i_u;
typedef __m128i __m128i_u;
typedef __m256  __m256_u;
typedef __m128  __m128_u;

typedef SSIZE_T ssize_t;

#define __attribute__(x)
#define __builtin_expect(x, y) (x)
#define __builtin_prefetch(x, ...)
#define fseeko _fseeki64
#define ftello _ftelli64
#define malloc_trim(x) (0)
#define M_MMAP_THRESHOLD 0
#define POSIX_FADV_DONTNEED 4

inline int mallopt(int param, int value) {
    (void)param; (void)value;
    return 1;
}

inline int posix_fadvise(int fd, int64_t offset, int64_t len, int advice) {
    (void)fd; (void)offset; (void)len; (void)advice;
    return 0;
}

inline ssize_t readlink(const char* path, char* buf, size_t bufsiz) {
    (void)path; (void)buf; (void)bufsiz;
    return -1;
}

inline void* memmem(const void* haystack, size_t haystacklen, const void* needle, size_t needlelen) {
    if (needlelen == 0) return (void*)haystack;
    if (haystacklen < needlelen) return nullptr;
    const char* h = (const char*)haystack;
    const char* n = (const char*)needle;
    for (size_t i = 0; i <= haystacklen - needlelen; ++i) {
        if (h[i] == n[0] && memcmp(h + i, n, needlelen) == 0) return (void*)(h + i);
    }
    return nullptr;
}

#ifndef __FMA__
#define __FMA__ 1
#endif

#ifndef __F16C__
#define __F16C__ 1
#endif

#ifndef S_IRUSR
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#endif

#ifndef S_IRWXU
#define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)
#endif

inline int __builtin_ctzll(unsigned long long x) {
    unsigned long idx;
    if (_BitScanForward64(&idx, x)) return (int)idx;
    return 64;
}

inline int __builtin_ctz(unsigned int x) {
    unsigned long idx;
    if (_BitScanForward(&idx, x)) return (int)idx;
    return 32;
}

inline int __builtin_clz(unsigned int x) {
    unsigned long idx;
    if (_BitScanReverse(&idx, x)) return (int)(31 - idx);
    return 32;
}

inline int __builtin_clzll(unsigned long long x) {
    unsigned long idx;
    if (_BitScanReverse64(&idx, x)) return (int)(63 - idx);
    return 64;
}

inline int __builtin_popcount(unsigned int x) {
    return (int)__popcnt(x);
}

inline int __builtin_popcountll(unsigned long long x) {
    return (int)__popcnt64(x);
}

inline void qsort_r(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*, void*), void* arg) {
    if (size == sizeof(size_t)) {
        size_t* arr = (size_t*)base;
        std::stable_sort(arr, arr + nmemb, [=](size_t a, size_t b) {
            return compar(&a, &b, arg) < 0;
        });
    }
}

inline FILE* fmemopen(void* buf, size_t size, const char* mode) {
    (void)mode;
    FILE* f = tmpfile();
    if (!f) return nullptr;
    if (size > 0 && buf) {
        fwrite(buf, 1, size, f);
        rewind(f);
    }
    return f;
}

#include <malloc.h>
namespace std {
inline void* aligned_alloc(size_t alignment, size_t size) {
    return _aligned_malloc(size, alignment);
}
}

#endif // _MSC_VER

#endif // MSVC_COMPAT_H
