#ifndef WIN_COMPAT_SYS_MMAN_H
#define WIN_COMPAT_SYS_MMAN_H

#include <stdint.h>
#include <stddef.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define MAP_NORESERVE 0x4000
#define MAP_FAILED ((void*)-1)

#define MADV_HUGEPAGE 14
#define MADV_DONTNEED 4
#define MADV_COLLAPSE 25

inline void* mmap(void* addr, size_t len, int prot, int flags, int fd, size_t offset) {
    (void)addr; (void)prot; (void)flags; (void)fd; (void)offset;
    void* p = VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!p) return MAP_FAILED;
    return p;
}

inline int munmap(void* addr, size_t len) {
    (void)len;
    if (addr && addr != MAP_FAILED) {
        VirtualFree(addr, 0, MEM_RELEASE);
    }
    return 0;
}

inline int madvise(void* addr, size_t len, int advice) {
    (void)addr; (void)len; (void)advice;
    return 0;
}

inline int mincore(void* addr, size_t len, unsigned char* vec) {
    (void)addr; (void)len; (void)vec;
    return -1;
}

#endif
