#ifndef __TERMIOS_H__
#define __TERMIOS_H__

#include "mylib.h"
#include "syscall.h"

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

// 宏定义
#define ICANON    0000002 // 规范模式
#define ECHO      0000010 // 回显
#define ISIG      0000001 // 信号
#define TCSANOW   0       // 立即生效
#define TCSADRAIN 1       // 排空输出后生效
#define TCSAFLUSH 2       // 排空并丢弃输入后生效

// 架构相关系统调用号及命令
#if defined(__x86_64__)
#define SYS_IOCTL 16
#define TCGETS    0x5401
#define TCSETS    0x5402
#define TCSETSW   0x5403 // 新增TCSADRAIN命令
#define TCSETSF   0x5404 // 新增TCSAFLUSH命令
#elif defined(__aarch64__)
#define SYS_IOCTL 29
#define TCGETS    0x403c7413
#define TCSETS    0x803c7414
#define TCSETSW   0x803c7415 // 需确认实际值
#define TCSETSF   0x803c7416 // 需确认实际值
#else
#error "Unsupported arch"
#endif

// 跨平台 termios 结构体 (需与内核头文件一致)
#if defined(__x86_64__) || defined(__aarch64__)
struct termios {
    unsigned int c_iflag;       // 输入模式
    unsigned int c_oflag;       // 输出模式
    unsigned int c_cflag;       // 控制模式
    unsigned int c_lflag;       // 本地模式
    unsigned char c_line;       // 线路规程
    unsigned char __padding[3]; // 实际内核可能在此处填充3字节 ?
    unsigned char c_cc[19];     // 控制字符
};
#else
#error "Termios structure not defined for this arch"
#endif

static long sys_ioctl(int fd, unsigned long cmd, void* arg) { return syscall(SYS_IOCTL, fd, cmd, arg); }

struct termios orig_termios;

// 跨平台函数
int tcgetattr(int fd, struct termios* t) {
    if (sys_ioctl(fd, TCGETS, t) != 0) {
        print("tcgetattr error", nullptr);
        return -1;
    }
    return 0;
}

int tcsetattr(int fd, int opt, const struct termios* t) {
    unsigned long cmd;
    switch (opt) {
    case TCSANOW:
        cmd = TCSETS;
        break;
    case TCSADRAIN:
        cmd = TCSETSW;
        break;
    case TCSAFLUSH:
        cmd = TCSETSF;
        break;
    default:
        return -1; // 错误处理
    }
    return sys_ioctl(fd, cmd, (void*)t) == 0 ? 0 : -1;
}

void enable_raw_mode() {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &orig_termios);
    raw = orig_termios;
    // 关闭规范模式、回显、信号处理
    // raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_lflag &= ~ICANON;
    // 设置最小读取字节数和超时
    raw.c_cc[VMIN] = 1;  // 至少读取1字节
    raw.c_cc[VTIME] = 0; // 无超时

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disable_raw_mode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

static_assert(offsetof(struct termios, c_lflag) == 12, "c_lflag offset mismatch");
static_assert(offsetof(struct termios, c_cc) == 20, "c_cc array offset error");
// 架构验证宏
#if defined(__x86_64__)
static_assert(sizeof(struct termios) == 40, "x86_64 termios size error");
#elif defined(__aarch64__)
static_assert(sizeof(struct termios) == 40, "ARM64 termios size error");
#endif

#endif // __TERMIOS_H__