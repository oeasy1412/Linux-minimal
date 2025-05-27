#ifndef __MEMORY_H__
#define __MEMORY_H__

#include "mylib.h"

static char mem[40960], *freem = mem;

void* zalloc(size_t sz) {
    freem = (char*)(((__intptr_t)freem + 7) & ~(__intptr_t)7);
    assert(freem + sz < mem + sizeof(mem));
    void* ret = freem;
    freem += sz;
    return ret;
}
void free(void* ptr) {
    // 简单实现不回收内存，实际需要根据内存管理器设计
}
char* strdup_z(const char* s) {
    if (!s) {
        return nullptr;
    }
    char* p = static_cast<char*>(zalloc(strlen(s) + 1));
    return strcpy(p, s);
}
void* memmove_z(void* dest, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    if (d < s) {
        for (size_t i = 0; i < n; ++i) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = n; i > 0;) {
            --i;
            d[i] = s[i];
        }
    }
    return dest;
}

#endif // __MEMORY_H__