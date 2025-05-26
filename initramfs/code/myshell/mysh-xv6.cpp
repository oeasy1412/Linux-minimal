// Linux port of xv6-riscv shell in C++ (no libc)
// gcc -g -O2 -c -ffreestanding -fno-exceptions -nostdlib mysh-xv6.cpp
// ld mysh-xv6.o -o mysh

#include "mylib.h"
#include "termios.h"

#include <fcntl.h>
#include <stdarg.h>
#include <sys/syscall.h>

// Parsed command representation
enum { EXEC = 1, REDIR = 2, PIPE = 3, LIST = 4, BACK = 5 };

// 历史记录
#define MAX_HISTORY 50
static char* history[MAX_HISTORY];
static int hist_count = 0; // 实际存储的历史数量
static int hist_pos = -1;  // 当前显示的历史索引
static int edit_pos = 0;   // 当前编辑位置
static char temp_buf[256]; // 保存当前未提交的输入

// my var
char** environ = nullptr; // 全局环境变量表指针
const char* path;

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

    if (count > 0)
        syscall(SYS_writev, 2, vecs, count);
}

#define assert(cond)                                                                                                   \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            print("Panicked.\n", nullptr);                                                                             \
            syscall(SYS_exit, 1);                                                                                      \
        }                                                                                                              \
    } while (0)

static char mem[4096], *freem = mem;

void* zalloc(size_t sz) {
    freem = (char*)(((__intptr_t)freem + 7) & ~(__intptr_t)7);
    assert(freem + sz < mem + sizeof(mem));
    void* ret = freem;
    freem += sz;
    return ret;
}
char* strdup_z(const char* s) {
    char* p = static_cast<char*>(zalloc(strlen(s) + 1));
    return strcpy(p, s);
}

char* getcwd(char* buf, size_t size) { return (syscall(SYS_getcwd, buf, size) >= 0) ? buf : nullptr; }

char* getenv(const char* name) {
    if (environ == nullptr) {
        print("Environment is empty.\n", nullptr);
        return nullptr;
    }
    size_t name_len = strlen(name);
    for (char** ep = environ; *ep; ++ep) {
        char* eq_pos = strchr(*ep, '=');
        if (eq_pos && (eq_pos - *ep) == name_len) {
            if (strncmp(*ep, name, name_len) == 0) {
                return eq_pos + 1;
            }
        }
    }
    return nullptr;
}

const char* findPath(const char* cmd) {
    static char buf[512];
    char* path_copy = static_cast<char*>(zalloc(strlen(path) + 1));
    strcpy(path_copy, path);
    char* dir = strtok(path_copy, ":");
    while (dir) {
        char* p = buf;
        strcpy(p, dir);
        p += strlen(dir);
        *p++ = '/';
        strcpy(p, cmd);
        p += strlen(cmd);
        if (syscall(SYS_access, buf, X_OK) == 0) {
            freem = path_copy; // 释放临时内存
            return buf;
        }
        dir = strtok(nullptr, ":");
    }
    freem = path_copy; // 释放临时内存
    return cmd;
}

// run cmd 保证不会return
void runcmd(struct cmd* cmd) {
    int p[2];
    const char* fullPath;
    struct backcmd* bcmd;
    struct execcmd* ecmd;
    struct listcmd* lcmd;
    struct pipecmd* pcmd;
    struct redircmd* rcmd;

    if (cmd == 0)
        syscall(SYS_exit, 1);

    switch (cmd->type) {
    case EXEC:
        ecmd = (struct execcmd*)cmd;
        if (ecmd->argv[0] == 0)
            syscall(SYS_exit, 1);
        fullPath = findPath(ecmd->argv[0]);
        syscall(SYS_execve, fullPath, ecmd->argv, environ);
        print("fail to exec ", ecmd->argv[0], "\n", nullptr);
        break;

    case REDIR:
        rcmd = (struct redircmd*)cmd;
        syscall(SYS_close, rcmd->fd);
        if (syscall(SYS_open, rcmd->file, rcmd->mode, 0644) < 0) {
            print("fail to open ", rcmd->file, "\n", nullptr);
            syscall(SYS_exit, 1);
        }
        runcmd(rcmd->cmd);
        break;

    case LIST:
        lcmd = (struct listcmd*)cmd;
        if (syscall(SYS_fork) == 0)
            runcmd(lcmd->left);
        syscall(SYS_wait4, -1, 0, 0, 0);
        runcmd(lcmd->right);
        break;

    case PIPE:
        pcmd = (struct pipecmd*)cmd;
        assert(syscall(SYS_pipe, p) >= 0);
        if (syscall(SYS_fork) == 0) {
            syscall(SYS_close, 1);    // stdout
            syscall(SYS_dup, p[1]);   // p[1] 管道的写口
            syscall(SYS_close, p[0]); // p[0] 管道的读口
            syscall(SYS_close, p[1]);
            runcmd(pcmd->left);
        }
        if (syscall(SYS_fork) == 0) {
            syscall(SYS_close, 0);
            syscall(SYS_dup, p[0]);
            syscall(SYS_close, p[0]);
            syscall(SYS_close, p[1]);
            runcmd(pcmd->right);
        }
        syscall(SYS_close, p[0]);
        syscall(SYS_close, p[1]);
        syscall(SYS_wait4, -1, 0, 0, 0);
        syscall(SYS_wait4, -1, 0, 0, 0);
        break;

    case BACK:
        bcmd = (struct backcmd*)cmd;
        if (syscall(SYS_fork) == 0)
            runcmd(bcmd->cmd);
        break;

    default:
        assert(0);
    }
    syscall(SYS_exit, 0);
}

int getcmd(char* buf, int nbuf) {
    struct termios orig_termios, new_termios;
    // 保存并修改终端设置
    syscall(SYS_ioctl, 0, 0x5401 /*TCGETS*/, &orig_termios);
    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    syscall(SYS_ioctl, 0, 0x5402 /*TCSETS*/, &new_termios);

    char input_buf[3]; // 输入缓冲区（考虑ESC序列）
    int esc_state = 0; // ESC状态机：0-正常 1-ESC 2-[

    // 初始化编辑状态 // memset(buf, 0, nbuf);
    edit_pos = 0;
    hist_pos = -1;

    char cwd[256];
    if (getcwd(cwd, sizeof(cwd))) {
        print("\033[38;2;102;204;255m", cwd, "\033[0m > ", nullptr);
    } else {
        print("? > ", nullptr);
    }
    char* pos = buf;
    int total_read = 0;

    while (total_read < nbuf - 1) {
        int remaining = nbuf - 1 - total_read;
        int nread = syscall(SYS_read, 0, pos, remaining);
        if (nread <= 0)
            return -1;

        char* nl = memchr(pos, '\n', nread);
        if (nl) {
            *nl = '\0';
            return 0;
        }

        pos += nread;
        total_read += nread;
    }

    buf[nbuf - 1] = '\0';
    return 0;
}

extern "C" void c_start(long* stack_ptr) {
    // 从栈中获取 argc, argv, envp (x86_64 调用约定)
    long argc = stack_ptr[0];
    char** argv = reinterpret_cast<char**>(stack_ptr + 1);
    char** envp = argv + argc + 1;

    environ = envp; // 保存全局环境变量表
    path = getenv("PATH");
    print("Welcome to use mysh, an unfriendly self-developed shell\n$PATH=", path, "\n", nullptr);

    if (environ == nullptr)
        print("Environment is empty.\n", nullptr);

    static char buf[100];

    while (getcmd(buf, sizeof(buf)) >= 0) {
        if (buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' ') {
            char* path = buf + 3;
            if (*path == '\0') {
                char* HOME = getenv("HOME");
                if (HOME) {
                    path = const_cast<char*>(HOME);
                } else {
                    print("cd: no HOME directory\n", nullptr);
                    continue;
                }
            }
            // Chdir must be called by the parent
            if (syscall(SYS_chdir, path) < 0)
                print("cannot cd ", path, "\n", nullptr);
            continue;
        }
        if (syscall(SYS_fork) == 0)
            runcmd(parsecmd(buf));
        syscall(SYS_wait4, -1, 0, 0, 0);
    }
    syscall(SYS_exit, 0);
}

// 纯汇编入口
__asm__(
    ".global _start\n"
    "_start:\n"
    "mov %rsp, %rdi\n" // 将原始栈指针作为参数传递
    "jmp c_start\n");

void save_current(char* buf, int max_len) {
    int len = strlen(buf);
    len = len < max_len - 1 ? len : max_len - 1;
    memcpy(temp_buf, buf, len);
    temp_buf[len] = '\0';
}
// 恢复当前输入
void restore_current(char* buf, int max_len) {
    size_t len = strlen(temp_buf);
    len = len < max_len - 1 ? len : max_len - 1;
    memcpy(buf, temp_buf, len);
    buf[len] = '\0';
    edit_pos = len;
}
// 加载历史记录
void load_history(char* buf, int max_len, int index) {
    if (index >= 0 && index < hist_count) {
        strncpy(buf, history[index], max_len);
        edit_pos = strlen(history[index]);
    }
}
// 重绘行
void redraw_line(const char* buf, const char* prompt, size_t cursor_pos) {
    // 清空行并重置光标
    print("\033[2K\r", prompt, buf, nullptr);
    // 定位光标
    if (cursor_pos > 0) {
        char seq[16];
        size_t pos = cursor_pos + strlen(prompt);
        print(seq, sizeof(seq), "\033[%dG", pos, nullptr);
        print(seq, nullptr);
    }
}

// 处理方向键
void handle_arrow(char c, char* buf, int max_len, const char* prompt) {
    if (c == 'A' && hist_count > 0) { // 上键
        if (hist_pos == -1) {         // 首次按上键
            hist_pos = hist_count - 1;
            save_current(buf, max_len);
        } else if (hist_pos > 0) {
            hist_pos--;
        }
        load_history(buf, max_len, hist_pos);
    } else if (c == 'B' && hist_pos != -1) { // 下键
        if (hist_pos < hist_count - 1) {
            hist_pos++;
            load_history(buf, max_len, hist_pos);
        } else {
            hist_pos = -1;
            restore_current(buf, max_len);
        }
    } else if (c == 'C') { /* 右键 */
        if (edit_pos < strlen(buf)) {
            edit_pos++;
        }
    } else if (c == 'D') { /* 左键 */
        if (edit_pos > 0) {
            edit_pos--;
        }
    }
    redraw_line(buf, prompt, strlen(buf));
}

void handle_backspace(char* buf, const char* prompt) {
    // if (edit_pos > 0) {
    //     memmove(buf + edit_pos-1, buf + edit_pos, strlen(buf) - edit_pos + 1);
    //     edit_pos--;
    //     redraw_line(buf, prompt, edit_pos);
    // }
}

// Constructors
struct cmd* execcmd(void) {
    struct execcmd* cmd;

    cmd = static_cast<struct execcmd*>(zalloc(sizeof(*cmd)));
    cmd->type = EXEC;
    return (struct cmd*)cmd;
}

struct cmd* redircmd(struct cmd* subcmd, char* file, char* efile, int mode, int fd) {
    struct redircmd* cmd;

    cmd = static_cast<struct redircmd*>(zalloc(sizeof(*cmd)));
    cmd->type = REDIR;
    cmd->cmd = subcmd;
    cmd->file = file;
    cmd->efile = efile;
    cmd->mode = mode;
    cmd->fd = fd;
    return (struct cmd*)cmd;
}

struct cmd* pipecmd(struct cmd* left, struct cmd* right) {
    struct pipecmd* cmd;

    cmd = static_cast<struct pipecmd*>(zalloc(sizeof(*cmd)));
    cmd->type = PIPE;
    cmd->left = left;
    cmd->right = right;
    return (struct cmd*)cmd;
}

struct cmd* listcmd(struct cmd* left, struct cmd* right) {
    struct listcmd* cmd;

    cmd = static_cast<struct listcmd*>(zalloc(sizeof(*cmd)));
    cmd->type = LIST;
    cmd->left = left;
    cmd->right = right;
    return (struct cmd*)cmd;
}

struct cmd* backcmd(struct cmd* subcmd) {
    struct backcmd* cmd;

    cmd = static_cast<struct backcmd*>(zalloc(sizeof(*cmd)));
    cmd->type = BACK;
    cmd->cmd = subcmd;
    return (struct cmd*)cmd;
}

// Parsing
char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

static inline const char* expand_var(char* s) {
    if (*s == '$') {
        const char* var_value = getenv(s + 1);
        return var_value ? var_value : "";
    }
    return s;
}

int gettoken(char** ps, char* es, char** q, char** eq) {
    char* s;
    int ret;

    s = *ps;
    while (s < es && strchr(whitespace, *s))
        s++;
    if (q)
        *q = s;
    ret = *s;
    switch (*s) {
    case 0:
        break;
    case '|':
    case '(':
    case ')':
    case ';':
    case '&':
    case '<':
        s++;
        break;
    case '>':
        s++;
        if (*s == '>') {
            ret = '+';
            s++;
        }
        break;
    default:
        ret = 'a';
        while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
            s++;
        break;
    }
    if (eq)
        *eq = s;

    while (s < es && strchr(whitespace, *s))
        s++;
    *ps = s;
    return ret;
}

int peek(char** ps, char* es, char* toks) {
    char* s;

    s = *ps;
    while (s < es && strchr(whitespace, *s))
        s++;
    *ps = s;
    return *s && strchr(toks, *s);
}

struct cmd* parseline(char**, char*);
struct cmd* parsepipe(char**, char*);
struct cmd* parseexec(char**, char*);
struct cmd* nulterminate(struct cmd*);

struct cmd* parsecmd(char* s) {
    char* es;
    struct cmd* cmd;

    es = s + strlen(s);
    cmd = parseline(&s, es);
    peek(&s, es, "");
    assert(s == es);
    nulterminate(cmd);
    return cmd;
}

struct cmd* parseline(char** ps, char* es) {
    struct cmd* cmd;

    cmd = parsepipe(ps, es);
    while (peek(ps, es, "&")) {
        gettoken(ps, es, 0, 0);
        cmd = backcmd(cmd);
    }
    if (peek(ps, es, ";")) {
        gettoken(ps, es, 0, 0);
        cmd = listcmd(cmd, parseline(ps, es));
    }
    return cmd;
}

struct cmd* parsepipe(char** ps, char* es) {
    struct cmd* cmd;

    cmd = parseexec(ps, es);
    if (peek(ps, es, "|")) {
        gettoken(ps, es, 0, 0);
        cmd = pipecmd(cmd, parsepipe(ps, es));
    }
    return cmd;
}

struct cmd* parseredirs(struct cmd* cmd, char** ps, char* es) {
    int tok;
    char *q, *eq;

    while (peek(ps, es, "<>")) {
        tok = gettoken(ps, es, 0, 0);
        assert(gettoken(ps, es, &q, &eq) == 'a');
        switch (tok) {
        case '<':
            cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
            break;
        case '>':
            cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREAT | O_TRUNC, 1);
            break;
        case '+': // >>
            cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREAT, 1);
            break;
        }
    }
    return cmd;
}

struct cmd* parseblock(char** ps, char* es) {
    struct cmd* cmd;

    assert(peek(ps, es, "("));
    gettoken(ps, es, 0, 0);
    cmd = parseline(ps, es);
    assert(peek(ps, es, ")"));
    gettoken(ps, es, 0, 0);
    cmd = parseredirs(cmd, ps, es);
    return cmd;
}

struct cmd* parseexec(char** ps, char* es) {
    char *q, *eq;
    int tok, argc;
    struct execcmd* cmd;
    struct cmd* ret;

    if (peek(ps, es, "("))
        return parseblock(ps, es);

    ret = execcmd();
    cmd = (struct execcmd*)ret;

    argc = 0;
    ret = parseredirs(ret, ps, es);
    while (!peek(ps, es, "|)&;")) {
        if ((tok = gettoken(ps, es, &q, &eq)) == 0)
            break;
        assert(tok == 'a');
        cmd->argv[argc] = q;
        cmd->eargv[argc] = eq;
        assert(++argc < MAXARGS);
        ret = parseredirs(ret, ps, es);
    }
    cmd->argv[argc] = 0;
    cmd->eargv[argc] = 0;
    return ret;
}

// NUL-terminate all the counted strings.
struct cmd* nulterminate(struct cmd* cmd) {
    int i;
    struct backcmd* bcmd;
    struct execcmd* ecmd;
    struct listcmd* lcmd;
    struct pipecmd* pcmd;
    struct redircmd* rcmd;

    if (cmd == 0)
        return 0;

    switch (cmd->type) {
    case EXEC:
        ecmd = (struct execcmd*)cmd;
        for (i = 0; ecmd->argv[i]; ++i)
            *ecmd->eargv[i] = 0;
        break;

    case REDIR:
        rcmd = (struct redircmd*)cmd;
        nulterminate(rcmd->cmd);
        *rcmd->efile = 0;
        break;

    case PIPE:
        pcmd = (struct pipecmd*)cmd;
        nulterminate(pcmd->left);
        nulterminate(pcmd->right);
        break;

    case LIST:
        lcmd = (struct listcmd*)cmd;
        nulterminate(lcmd->left);
        nulterminate(lcmd->right);
        break;

    case BACK:
        bcmd = (struct backcmd*)cmd;
        nulterminate(bcmd->cmd);
        break;
    }
    return cmd;
}
