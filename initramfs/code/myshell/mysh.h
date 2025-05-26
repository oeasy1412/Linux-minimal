#ifndef __MYSH_H__
#define __MYSH_H__

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

#endif // __MYSH_H__