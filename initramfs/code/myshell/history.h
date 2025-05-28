#ifndef __HISTORY_H__
#define __HISTORY_H__

#include "memory.h"
#include "mylib.h"

// 光标操作宏定义
#define CURSOR_SAVE    "\033[s"  // 保存光标位置
#define CURSOR_RESTORE "\033[u"  // 恢复光标位置
#define CLEAR_LINE     "\033[2K" // 清除整行

// 历史记录
#define MAX_HISTORY 50
static char* history[MAX_HISTORY];
static int hist_count = 0;  // 实际存储的历史数量
static int hist_pos = -1;   // 当前显示的历史索引
static size_t edit_pos = 0; // 当前编辑位置
static char temp_buf[256];  // 保存当前未提交的输入
// static bool mid_edit = false;

void save_current(char* buf, int max_len);
void restore_current(char* buf, int max_len);
void load_history(char* buf, int max_len, int index);
void refresh_line(const char* buf, const char* prompt, size_t edit_pos);

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
    } else if (c == 'C') { // 右键
        if (edit_pos < strlen(buf)) {
            edit_pos++;
        }
    } else if (c == 'D') { // 左键
        if (edit_pos > 0) {
            edit_pos--;
        }
    }
    refresh_line(buf, prompt, edit_pos);
}

// 添加历史记录
void add_history(const char* buf) {
    if (edit_pos > 0) {
        if (hist_count >= MAX_HISTORY) {
            free(history[0]);
            memmove_z(history, history + 1, (MAX_HISTORY - 1) * sizeof(char*));
            hist_count--;
        }
        if (hist_count != 0) {
            const char* last = history[hist_count - 1];
            if (strncmp(last, buf, strlen(buf)) == 0) {
                return; // 内容不同才添加
            }
        }
        history[hist_count++] = strdup_z(buf);
    }
}

// 保存当前输入
void save_current(char* buf, int max_len) {
    size_t len = strlen(buf);
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

// 刷新行
void refresh_line(const char* buf, const char* prompt, size_t edit_pos) {
    print("\033[2K\r", nullptr);
    if (prompt) {
        print("\033[38;2;102;204;255m", prompt, "\033[0m > ", nullptr);
    } else {
        print("? > ", nullptr);
    }
    // fflush(buf);
    print(buf, nullptr);
    size_t prompt_len = 0;
    if (prompt) {
        prompt_len = strlen(prompt) + 3;
    } else {
        prompt_len = 3;
    }
    // 计算光标最终位置 (提示符长度 + 当前编辑位置)
    size_t total_pos = prompt_len + edit_pos;
    printf("\r\033[%dC", total_pos);
}

// handle
void handle_backspace(char* buf, const char* prompt) {
    size_t len = strlen(buf);
    if (edit_pos > 0 && edit_pos <= len) {
        memmove_z(buf + edit_pos - 1, buf + edit_pos, len - edit_pos + 1);
        edit_pos--;
        buf[len - 1] = '\0';
    }
    refresh_line(buf, prompt, edit_pos);
}

void handle_delete(char* buf, const char* prompt) {
    size_t len = strlen(buf);
    if (edit_pos < len) {
        // 安全删除，确保不超过缓冲区
        if (len < 256 - 1) {
            memmove_z(buf + edit_pos, buf + edit_pos + 1, len - edit_pos);
            buf[len - 1] = '\0';
        }
    }
    refresh_line(buf, prompt, edit_pos);
}

#endif // __HISTORY_H__