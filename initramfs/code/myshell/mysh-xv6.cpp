// Linux port of xv6-riscv shell in C++ (no libc)
// gcc -g -O2 -c -ffreestanding -nostdlib -fno-exceptions mysh-xv6.cpp
// ld mysh-xv6.o -o mysh

#include "history.h"
#include "memory.h"
#include "mylib.h"
#include "mysh.h"
#include "termios.h"

// my var
char** environ = nullptr; // 全局环境变量表指针
const char* path;

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
    enable_raw_mode(); // 进入原始模式

    char cwd[256];
    if (getcwd(cwd, sizeof(cwd))) {
        print("\033[38;2;102;204;255m", cwd, "\033[0m > ", nullptr);
    } else {
        print("? > ", nullptr);
    }
    // 初始化编辑状态
    memset(buf, 0, nbuf);
    int esc_state = 0; // ESC状态机：0-正常 1-ESC 2-[
    edit_pos = 0;
    hist_pos = -1;
    while (true) {
        char ch;
        int nread = syscall(SYS_read, 0, &ch, 1); // 单字符读取
        if (nread <= 0) {
            buf[0] = '\0';
            break;
        }
        // ESC序列处理
        if (esc_state != 0) {
            if (esc_state == 1 && ch == '[') {
                esc_state++;
            } else if (esc_state == 2) {
                esc_state = 0;
                if (ch == '3') {
                    char next_ch;
                    if (syscall(SYS_read, 0, &next_ch, 1) > 0 && next_ch == '~') {
                        handle_delete(buf, cwd);
                    }
                    continue;
                }
                handle_arrow(ch, buf, nbuf, cwd);
            } else {
                esc_state = 0;
            }
            continue;
        }

        if (ch >= 32 && ch < 127) { // 可打印字符
            size_t len = strlen(buf);
            if (len < nbuf - 1) { // 确保有空间插入
                memmove_z(buf + edit_pos + 1, buf + edit_pos, len - edit_pos + 1);
                buf[edit_pos++] = ch;
                if (edit_pos > len) {
                    buf[edit_pos] = '\0';
                }
            }
            refresh_line(buf, cwd, edit_pos);
        } else if (ch == 127 || ch == '\b') { // 退格
            handle_backspace(buf, cwd);
        } else if (ch == '\n') { // 提交命令
            add_history(buf);
            break;
        } else if (ch == 0x1B) { // ESC
            esc_state = 1;
        }
    }
    disable_raw_mode(); // 恢复终端设置
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

    static char buf[100];
    while (getcmd(buf, sizeof(buf)) >= 0) {
        if (buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' ') {
            char* cdpath = buf + 3;
            if (*cdpath == '\0') {
                /*static*/ char* HOME = getenv("HOME");
                if (HOME) {
                    cdpath = const_cast<char*>(HOME);
                } else {
                    print("cd: no HOME directory\n", nullptr);
                    continue;
                }
            }
            // Chdir must be called by the parent
            if (syscall(SYS_chdir, cdpath) < 0)
                print("cannot cd ", cdpath, "\n", nullptr);
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

// Parsing ------------------------------------------------------
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

int peek(char** ps, char* es, const char* toks) {
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
