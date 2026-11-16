#ifndef WIN_COMPAT_ELF_H
#define WIN_COMPAT_ELF_H

#include <stdint.h>

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

#define ElfW(type) Elf64_ ## type
#define PT_PHDR 6
#define PT_LOAD 1
#define PF_W 2
#define AT_PHDR 3
#define AT_PHNUM 4

inline unsigned long getauxval(unsigned long type) {
    (void)type;
    return 0;
}

#endif
