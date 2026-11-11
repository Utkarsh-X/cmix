#ifndef MSVC_COMPAT_H
#define MSVC_COMPAT_H

#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <immintrin.h>
#include <intrin.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <algorithm>

#define __attribute__(x)
#define __builtin_expect(x, y) (x)
#define __builtin_prefetch(x, ...)
#define fseeko _fseeki64
#define ftello _ftelli64

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

#endif // _MSC_VER

#endif // MSVC_COMPAT_H
