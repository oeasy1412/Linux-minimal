#ifndef __MY_LIB__
#define __MY_LIB__

#include <cstddef>

#define MAXARGS 10

struct cmd {
    int type;
};

struct execcmd {
    int type;
    char *argv[MAXARGS], *eargv[MAXARGS];
};

struct redircmd {
    int type, fd, mode;
    char *file, *efile;
    struct cmd* cmd;
};

struct pipecmd {
    int type;
    struct cmd *left, *right;
};

struct listcmd {
    int type;
    struct cmd *left, *right;
};

struct backcmd {
    int type;
    struct cmd* cmd;
};

struct cmd* parsecmd(char*);

// System call vector structure for writev
// struct iovec {
//     void*  iov_base;
//     size_t iov_len;
// };

// Minimum runtime library
static inline size_t strlen(const char* s) {
    size_t len = 0;
    for (; *s; ++s)
        ++len;
    return len;
}
static inline char* strchr(const char* s, int c) {
    for (; *s; ++s) {
        if (*s == c)
            return (char*)s;
    }
    return nullptr;
}
static inline char* memchr(const void* s, int c, size_t n) {
    const char* p = (const char*)s;
    for (size_t i = 0; i < n; ++i)
        if (p[i] == c)
            return (char*)(p + i);
    return nullptr;
}
static inline int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (s1[i] != s2[i])
            return static_cast<unsigned char>(s1[i]) - static_cast<unsigned char>(s2[i]);
        if (s1[i] == '\0')
            break;
    }
    return 0;
}
static inline char* strcpy(char* dest, const char* src) {
    char* original_dest = dest;
    while (*src != '\0') {
        *dest = *src;
        ++dest;
        ++src;
    }
    *dest = '\0';
    return original_dest;
}
static inline size_t strspn(const char* s, const char* accept) {
    const char* p = s;
    while (*p && strchr(accept, *p))
        p++;
    return p - s;
}
static inline size_t strcspn(const char* s, const char* reject) {
    const char* p = s;
    while (*p && !strchr(reject, *p))
        p++;
    return p - s;
}
static inline char* strtok_r(char* str, const char* delim, char** saveptr) {
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
static inline char* strtok(char* str, const char* delim) {
    static char* saved;
    return strtok_r(str, delim, &saved);
}

#endif // __MY_LIB__