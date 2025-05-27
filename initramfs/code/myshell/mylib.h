#ifndef __MYLIB_H__
#define __MYLIB_H__

#include "syscall.h"

#include <cstddef>
#include <fcntl.h>

#define LEFT    0x01
#define PLUS    0x02
#define SPACE   0x04
#define ZEROPAD 0x08 // 用 0 填充,比如 %8d 在位数不足时填充前导0
#define SIGN    0x10
#define LARGE   0x20 // 大写标记

#define is_digit(c) ((c) >= '0' && (c) <= '9')
static int get_wid(const char** s);
static char* number_to_string(char* str, long num, int base, int size, int type);

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
            print("\nassert panic.\n", nullptr);                                                                       \
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

int vsprintf(char* out, const char* fmt, va_list ap) {
    int len, i; // 长度
    unsigned long num;
    char* str;
    char* s;
    int flags = 0;     // 用来指示类型
    int integer_width; // 整数的长度 如%8d, 8为精度; qualifier: h(短整)或者l(长整)
    int base;          // 进制

    for (str = out; *fmt; fmt++) {
        // 还没有出现需要转化的常规字符
        if (*fmt != '%') {
            *str++ = *fmt;
            continue;
        }
        flags = 0;
    repeat:
        fmt++; // 跳过%, 判断类型

        // 第一个分支格式的判断
        switch (*fmt) {
        case '-':
            flags |= LEFT;
            goto repeat;
        case '+':
            flags |= PLUS;
            goto repeat;
        case ' ':
            flags |= SPACE;
            goto repeat;
        case '0':
            flags |= ZEROPAD;
            goto repeat; // 补充前导 0
        }

        // 获得整数的精度
        integer_width = -1;
        if (is_digit(*fmt)) {
            integer_width = get_wid(&fmt);
        }

        base = 10; // 默认基
        switch (*fmt) {
        // 指针
        case 'p':
            if (integer_width == -1) {
                integer_width = 2 * sizeof(void*);
                flags |= ZEROPAD;
            }
            *str++ = '0', *str++ = 'x';
            str = number_to_string(str, (unsigned long)va_arg(ap, void*), 16, integer_width, flags);
            continue;
        // 单字符
        case 'c':
            *str++ = (unsigned char)va_arg(ap, int);
            continue; // 跳到最外层的for循环
        // 字符串
        case 's':
            s = va_arg(ap, char*);
            if (!s)
                s = nullptr;
            len = strlen(s);
            for (i = 0; i < len; ++i)
                *str++ = *s++;
            continue; // 跳到最外层的for循环

        case 'o':
            base = 8;
            break; // break之后进入num的获取
        case 'x':
            base = 16;
            break;
        case 'd':
            flags |= SIGN;
        case 'u':
            break;

        default:
            assert(0);
            if (*fmt != '%')
                *str++ = '%';
            if (*fmt) {
                *str++ = *fmt;
            } else {
                --fmt;
            }
            continue;
        }

        // 如果是整型,分两种情况:带不带符号
        if (flags & SIGN) {
            num = va_arg(ap, int);
        } else {
            num = va_arg(ap, unsigned int);
        }

        // 将数字转化为字符
        str = number_to_string(str, num, base, integer_width, flags);
    }
    *str = '\0';
    return str - out;
}

int printf(const char* fmt, ...) {
    char out[2048];
    va_list va;
    va_start(va, fmt);
    int ret = vsprintf(out, fmt, va);
    va_end(va);
    print(out, nullptr);
    return ret;
}

static int get_wid(const char** s) {
    int i = 0;
    while (is_digit(**s)) {
        i = i * 10 + *((*s)++) - '0';
    }
    return i;
}
static char* number_to_string(char* str, long num, int base, int size, int type) {
    const char* dig = "0123456789abcdefghijklmnopqrstuvwxyz";
    char c, sign, temp[70];
    int i = 0;
    c = (type & ZEROPAD) ? '0' : ' '; // 是否补充前导 0
    sign = 0;

    if (type & SIGN) {
        if (num < 0) {
            num = -num;
            sign = '-';
            size--;
        } else if (type & PLUS) {
            sign = '+';
            size--;
        } else if (type & SPACE) {
            sign = ' ';
            size--;
        }
    }

    if (num == 0) {
        temp[i++] = '0';
    } else {
        while (num != 0) {
            temp[i++] = dig[((unsigned long)num) % (unsigned)base];
            num = ((unsigned long)num) / (unsigned)base;
        }
    }

    size -= i;

    if (!(type & (ZEROPAD | LEFT)))
        while (size-- > 0)
            *str++ = ' ';

    if (sign)
        *str++ = sign;

    if (!(type & LEFT))
        while (size-- > 0)
            *str++ = c; // 补充前导(0或者space)
    while (i-- > 0)
        *str++ = temp[i];
    while (size-- > 0)
        *str++ = ' ';

    return str;
}

#endif // __MYLIB_H__