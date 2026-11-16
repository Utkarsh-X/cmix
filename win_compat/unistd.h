#ifndef WIN_COMPAT_UNISTD_H
#define WIN_COMPAT_UNISTD_H

#include <io.h>
#include <process.h>
#include <direct.h>
#include <stdint.h>
#include <stddef.h>

inline void* sbrk(intptr_t increment) {
    (void)increment;
    return (void*)0;
}

#endif
