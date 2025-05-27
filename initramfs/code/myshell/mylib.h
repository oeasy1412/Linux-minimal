#ifndef __MYLIB_H__
#define __MYLIB_H__

#include "syscall.h"

#include <cstddef>
#include <fcntl.h>

// Minimum runtime library
inline size_t strlen(const char* s) {
    size_t len = 0;
    for (; *s; ++s)
        ++len;
    return len;
}

void print(const char* s, ...) {
    va_list ap;
    va_start(ap, s);
    struct iovec vecs[16];
    size_t count = 0;
    while (s && count < 16) {
        vecs[count].iov_base = (void*)s;
        vecs[count].iov_len = strlen(s);
        count++;
        s = va_arg(ap, const char*);
    }
    va_end(ap);

    if (count > 0) {
        syscall(SYS_writev, 2, vecs, count);
    }
}

#define assert(cond)                                                                                                   \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            print("Panicked.\n", nullptr);                                                                             \
            syscall(SYS_exit, 1);                                                                                      \
        }                                                                                                              \
    } while (0)

inline char* strchr(const char* s, int c) {
    for (; *s; ++s) {
        if (*s == c)
            return (char*)s;
    }
    return nullptr;
}
inline char* memchr(const void* s, int c, size_t n) {
    const char* p = (const char*)s;
    for (size_t i = 0; i < n; ++i)
        if (p[i] == c)
            return (char*)(p + i);
    return nullptr;
}
inline int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (s1[i] != s2[i])
            return static_cast<unsigned char>(s1[i]) - static_cast<unsigned char>(s2[i]);
        if (s1[i] == '\0')
            break;
    }
    return 0;
}
inline char* strcpy(char* dest, const char* src) {
    char* original_dest = dest;
    while (*src != '\0') {
        *dest = *src;
        ++dest;
        ++src;
    }
    *dest = '\0';
    return original_dest;
}
inline char* strncpy(char* dest, const char* src, size_t n) {
    char* original_dest = dest;
    size_t copied = 0;
    while (copied < n && *src != '\0') {
        *dest = *src;
        ++dest;
        ++src;
        ++copied;
    }
    while (copied < n) {
        *dest = '\0';
        ++dest;
        ++copied;
    }

    return original_dest;
}
inline void* memcpy(void* dest, const void* src, size_t n) {
    // char* d = static_cast<char*>(dest);
    // const char* s = static_cast<const char*>(src);
    // char* const end = d + n;
    // while (d != end) {
    //     *d = *s;
    //     ++d;
    //     ++s;
    // }
    // return dest;
    unsigned long int* d64 = static_cast<unsigned long int*>(dest);
    const unsigned long int* s64 = static_cast<const unsigned long int*>(src);
    while (n >= 8) {
        *d64++ = *s64++;
        n -= 8;
    }
    char* d = reinterpret_cast<char*>(d64);
    const char* s = reinterpret_cast<const char*>(s64);
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dest;
}
inline void* memset(void* s, int c, size_t n) {
    char* p = static_cast<char*>(s);
    const unsigned char uc = static_cast<unsigned char>(c);
    char* const end = p + n;
    while (p != end) {
        *p = uc;
        ++p;
    }

    return s;
}
inline size_t strspn(const char* s, const char* accept) {
    const char* p = s;
    while (*p && strchr(accept, *p))
        ++p;
    return p - s;
}
inline size_t strcspn(const char* s, const char* reject) {
    const char* p = s;
    while (*p && !strchr(reject, *p))
        ++p;
    return p - s;
}
inline char* strtok_r(char* str, const char* delim, char** saveptr) {
    char* p = str ? str : *saveptr;
    if (!p)
        return nullptr;

    p += strspn(p, delim);
    if (!*p) {
        *saveptr = nullptr;
        return nullptr;
    }

    char* end = p + strcspn(p, delim);
    if (*end)
        *end++ = '\0';
    *saveptr = end;
    return p;
}
inline char* strtok(char* str, const char* delim) {
    static char* saved;
    return strtok_r(str, delim, &saved);
}

void fflush(const char* buf) { syscall(SYS_write, 1, buf, strlen(buf)); }

#endif // __MYLIB_H__