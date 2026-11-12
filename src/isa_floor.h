#ifndef ISA_FLOOR_H_
#define ISA_FLOOR_H_

#if defined(_MSC_VER) && defined(__AVX2__)
#ifndef __FMA__
#define __FMA__ 1
#endif
#endif

#if !defined(__AVX2__) || !defined(__FMA__)
#error "this compressor requires AVX2+FMA (build with -march=x86-64-v3 or higher)"
#endif

#endif
