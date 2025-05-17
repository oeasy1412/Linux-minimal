#include <sys/syscall.h>

.globl _start
_start:
    movq $SYS_write, %rax    # write(
    movq $1, %rdi            #   fd=1,
    movq $st, %rsi           #   buf=st,
    movq $(ed - st), %rdx    #   count=ed-st
    syscall                  # );

    movq $SYS_exit, %rax     # exit(
    movq $0, %rdi            #   status=0
    syscall                  # );

st:
    .ascii "\033[01;33;46mHello,World!\033[0m\n"
ed:

// strace -T ./a.out &| vim -