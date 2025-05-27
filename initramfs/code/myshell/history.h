#ifndef __HISTORY_H__
#define __HISTORY_H__

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
}

// handle
void handle_backspace(char* buf, const char* prompt) {
    if (edit_pos > 0) {
        buf[--edit_pos] = '\0';
        refresh_line(buf, prompt, edit_pos);
    }
}

#endif // __HISTORY_H__