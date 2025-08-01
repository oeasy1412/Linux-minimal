#ifndef __MYSH_H__
#define __MYSH_H__

#define MAXARGS 10

#include "memory.h"

// Parsed command representation
enum { EXEC = 1, REDIR = 2, PIPE = 3, LIST = 4, BACK = 5 };

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

#endif // __MYSH_H__