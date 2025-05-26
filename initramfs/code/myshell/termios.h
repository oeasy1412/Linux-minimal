#ifndef __TERMIOS__
#define __TERMIOS__

#include <cstdarg>
#include <cstddef>
#include <sys/syscall.h>

// 添加必要的宏定义
#define ICANON    0000002 // 规范模式标志（八进制表示）
#define ECHO      0000010 // 回显控制标志
#define ISIG      0000001 // 信号控制标志
#define TCSANOW   0       // 立即生效
#define TCSADRAIN 1       // 在输出队列排空后生效
#define TCSAFLUSH 2       // 在排空并丢弃输入后生效

// 控制字符索引
enum {
    VINTR = 0,  // Ctrl+C (中断)
    VQUIT = 1,  // Ctrl+\ (退出)
    VERASE = 2, // Backspace (删除)
    VKILL = 3,  // Ctrl+U (删除行)
    VEOF = 4,   // Ctrl+D (文件结尾)
    VTIME = 5,  // 非规范模式超时
    VMIN = 6,   // 最小读取字符数
    VSWTC = 7   // 切换字符
};

// 多架构系统调用号定义
#if defined(__x86_64__)
#define SYS_IOCTL 16
#define TCGETS    0x5401
#define TCSETS    0x5402
#elif defined(__aarch64__)
#define SYS_IOCTL 29
#define TCGETS    0x403c7413 // ARM64 的特殊值
#define TCSETS    0x803c7414
#else
#error "Unsupported architecture"
#endif

// 跨平台 termios 结构体 (需与内核头文件一致)
#if defined(__x86_64__) || defined(__aarch64__)
struct termios {
    unsigned long c_iflag;  // 输入模式标志
    unsigned long c_oflag;  // 输出模式标志
    unsigned long c_cflag;  // 控制模式标志
    unsigned long c_lflag;  // 本地模式标志
    unsigned char c_line;   // 线路规程
    unsigned char c_cc[19]; // 控制字符
};
#else
#error "Termios structure not defined for this arch"
#endif

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
static long sys_ioctl(int fd, unsigned long cmd, void* arg) { return syscall(SYS_IOCTL, fd, cmd, arg); }

// 跨平台终端设置函数
int tcgetattr(int fd, struct termios* t) { return sys_ioctl(fd, TCGETS, t) == 0 ? 0 : -1; }

int tcsetattr(int fd, int opt, const struct termios* t) {
    // TCSANOW 等选项转换
    unsigned long cmd = (opt == TCSANOW) ? TCSETS : 0;
    return sys_ioctl(fd, cmd, (void*)t) == 0 ? 0 : -1;
}

void disable_echo_and_canonical() {
    struct termios orig, newt;

    // 获取当前终端设置
    if (sys_ioctl(0, TCGETS, &orig) < 0) {
        // 错误处理
        return;
    }

    // 修改标志位
    newt = orig;
    newt.c_lflag &= ~(ICANON | ECHO); // 清除ICANON和ECHO位

    // 应用新设置
    if (sys_ioctl(0, TCSETS, &newt) < 0) {
        // 错误处理
    }
}

void save_current(char* buf, int max_len);
void restore_current(char* buf, int max_len);
void load_history(char* buf, int max_len, int index);
void redraw_line(const char* buf, const char* prompt, size_t cursor_pos);
void handle_arrow(char c, char* buf, int max_len, const char* prompt);

static_assert(offsetof(struct termios, c_lflag) == 24, "c_lflag offset mismatch");
static_assert(offsetof(struct termios, c_cc) == 33, "c_cc array offset error");
// 架构验证宏
#if defined(__x86_64__)
static_assert(sizeof(struct termios) == 56, "x86_64 termios size error");
#elif defined(__aarch64__)
static_assert(sizeof(struct termios) == 56, "ARM64 termios size error");
#endif

#endif // __TERMIOS__