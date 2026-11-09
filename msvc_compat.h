#ifndef MSVC_COMPAT_H
#define MSVC_COMPAT_H

#ifdef _MSC_VER
#define __attribute__(x)
#define __builtin_expect(x, y) (x)
#define __builtin_prefetch(x, ...)
#define fseeko _fseeki64
#define ftello _ftelli64
#endif

#endif
