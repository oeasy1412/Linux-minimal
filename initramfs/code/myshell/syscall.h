#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include <cstdarg>
#include <cstddef>
#include <sys/syscall.h>

/* Standard file descriptors.  */
#define STDIN_FILENO  0 /* Standard input.  */
#define STDOUT_FILENO 1 /* Standard output.  */
#define STDERR_FILENO 2 /* Standard error output.  */

long syscall(int num, ...) {
    va_list ap;
    va_start(ap, num);
    register long a0 asm("rax") = num;
    register long a1 asm("rdi") = va_arg(ap, long);
    register long a2 asm("rsi") = va_arg(ap, long);
    register long a3 asm("rdx") = va_arg(ap, long);
    register long a4 asm("r10") = va_arg(ap, long);
    va_end(ap);
    asm volatile("syscall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a4) : "memory", "rcx", "r8", "r9", "r11");
    return a0;
}

#endif // __SYSCALL_H__