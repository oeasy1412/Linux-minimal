#include "HistoryManager.h"

#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// 原始终端配置保存
static struct termios orig_termios;

char* getcwd(char* buf, size_t size) { return (syscall(SYS_getcwd, buf, size) >= 0) ? buf : nullptr; }

// 启用原始模式
void enable_raw_mode() {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &orig_termios);
    raw = orig_termios;
    raw.c_lflag &= ~ICANON;
    // raw.c_lflag &= ~(ICANON | ECHO | ISIG); // 关闭规范模式、回显、信号处理
    raw.c_cc[VMIN] = 1;  // 最小读取1字符
    raw.c_cc[VTIME] = 0; // 无超时
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// 恢复终端原始设置
void disable_raw_mode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

enum class TokenType {
    ARG,             // 默认
    REDIRECT_IN,     // <
    REDIRECT_OUT,    // >
    REDIRECT_APPEND, // >>
    REDIRECT_ERROR   // 2>
};

struct Token {
    std::string value;
    TokenType type;
};

// 词法分析器（支持引号和重定向符号）
std::vector<Token> tokenize(const std::string& input) {
    std::vector<Token> tokens;
    size_t pos = 0;
    const size_t len = input.length();

    while (pos < len) {
        // 跳过空白字符
        while (pos < len && std::isspace(input[pos]))
            ++pos;
        if (pos >= len)
            break;

        // 检测重定向符号
        if (input[pos] == '<' || input[pos] == '>') {
            const char c = input[pos];
            TokenType type = TokenType::REDIRECT_IN;

            // 判断符号类型
            if (c == '>') {
                // 检测是否追加模式 >>
                if (pos + 1 < len && input[pos + 1] == '>') {
                    type = TokenType::REDIRECT_APPEND;
                    tokens.push_back({">>", type});
                    pos += 2;
                    continue;
                }
                type = TokenType::REDIRECT_OUT;
            }

            tokens.push_back({std::string(1, c), type});
            ++pos;
            continue;
        }

        // 处理带引号的参数
        if (input[pos] == '"') {
            std::string value;
            ++pos; // 跳过起始引号
            while (pos < len && input[pos] != '"') {
                value += input[pos++];
            }
            if (pos < len)
                ++pos; // 跳过结束引号
            tokens.push_back({value, TokenType::ARG});
            continue;
        }

        // 收集普通参数
        std::string value;
        while (pos < len && !std::isspace(input[pos]) && input[pos] != '<' && input[pos] != '>') {
            value += input[pos++];
        }
        tokens.push_back({value, TokenType::ARG});
    }
    return tokens;
}

// 命令基类
struct Command {
    virtual void execute() = 0;
    virtual ~Command() = default;
};
// 普通执行
struct ExecCommand : Command {
    std::vector<std::string> args;
    std::function<void(std::vector<std::string>&)> callback;

    explicit ExecCommand(std::vector<std::string> a, std::function<void(std::vector<std::string>&)> callback_fun)
        : args(std::move(a)),
          callback(std::move(callback_fun)) {}

    void execute() override {
        if (args.empty()) {
            return;
        }
        callback(const_cast<std::vector<std::string>&>(args));
    }
};
// 重定向
struct RedirCommand : Command {
    std::unique_ptr<Command> child;
    std::string input_file;
    std::string output_file;
    bool append = false;

    void execute() override {
        // 备份原始文件描述符
        int saved_stdin = dup(STDIN_FILENO);
        int saved_stdout = dup(STDOUT_FILENO);
        try {
            // 输入重定向
            if (!input_file.empty()) {
                int fd = open(input_file.c_str(), O_RDONLY);
                if (fd == -1) {
                    throw std::runtime_error("Fail open O_RDONLY: " + input_file);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            // 输出重定向
            if (!output_file.empty()) {
                int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
                int fd = open(output_file.c_str(), flags, 0644);
                if (fd == -1) {
                    throw std::runtime_error("Fail open : O_CREAT" + output_file);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            // 执行子命令
            if (child) {
                child->execute();
            }
        } catch (...) {
            // 恢复标准输入输出
            dup2(saved_stdin, STDIN_FILENO);
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdin);
            close(saved_stdout);
            throw;
        }
        // 恢复标准输入输出
        dup2(saved_stdin, STDIN_FILENO);
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdin);
        close(saved_stdout);
    }
};
// 管道
struct PipeCommand : Command {
    std::unique_ptr<Command> left;
    std::unique_ptr<Command> right;

    void execute() override {
        int pipefd[2];
        if (pipe(reinterpret_cast<int*>(pipefd)) < 0) {
            throw std::runtime_error("Failed to create pipe");
        }

        // 左进程（写端）
        if (fork() == 0) {
            close(pipefd[0]);   // 关闭读端
            dup2(pipefd[1], 1); // 重定向stdout到管道
            close(pipefd[1]);
            left->execute();
            exit(EXIT_SUCCESS);
        }

        // 右进程（读端）
        if (fork() == 0) {
            close(pipefd[1]);   // 关闭写端
            dup2(pipefd[0], 0); // 重定向stdin到管道
            close(pipefd[0]);
            right->execute();
            exit(EXIT_SUCCESS);
        }

        // 父进程
        close(pipefd[0]);
        close(pipefd[1]);
        wait(nullptr);
        wait(nullptr);
    }
};

class Shell {
  private:
    HistoryManager& history;
    std::vector<std::string> path_dirs;
    std::string current_prompt;
    std::string buf, temp_buf;
    size_t edit_pos = 0;
    int hist_index = -1;
    bool last_key_was_tab = false;
    typename std::list<HistoryManager::Node*>::iterator history_pos;

  public:
    explicit Shell(HistoryManager& hist) : history(hist) { setpgid(0, 0); }
    void run() {
        setup_environment();
        main_loop();
    }

    static Shell* instance;
    static void sigint_handler(int sig);

  private:
    // PATH
    void setup_environment() {
        if (const char* path = std::getenv("PATH")) {
            split_path(path);
        }
        update_prompt();
    }
    void split_path(const char* path) {
        std::istringstream iss(path);
        std::string dir;
        while (std::getline(iss, dir, ':')) {
            path_dirs.emplace_back(dir);
        }
    }

    std::string find_executable(const std::string& cmd) const {
        if (cmd.find('/') != std::string::npos) {
            return fs::exists(cmd) ? cmd : "";
        }

        for (const auto& dir : path_dirs) {
            fs::path full_path = fs::path(dir) / cmd;
            if (fs::exists(full_path) &&
                (fs::status(full_path).permissions() & fs::perms::owner_exec) != fs::perms::none) {
                return full_path.string();
            }
        }
        return "";
    }

    void execute_command(const std::vector<std::string>& args) {
        if (args.empty()) {
            return;
        }
        // 处理内置命令
        if (args[0] == "cd") {
            handle_cd(args);
            return;
        }

        // 查找可执行文件
        std::string full_path = find_executable(args[0]);
        if (full_path.empty()) {
            std::cerr << "Command not found: " << args[0] << std::endl;
            return;
        }

        // 准备execvp参数
        std::vector<char*> argv;
        argv.reserve(args.size());
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        // 创建子进程
        pid_t pid = fork();
        if (pid == 0) {
            execvp(full_path.c_str(), argv.data());
            perror("execvp failed");
            exit(EXIT_FAILURE);
        } else if (pid > 0) {
            waitpid(pid, nullptr, 0);
        } else {
            perror("fork failed");
        }
    }

    // impl_history
    void navigate_history(bool up) {
        const std::vector<std::string>& hist_list = history.get_history();
        if (up) {
            if (hist_index == -1) { // 首次按上键
                temp_buf = buf;
            }
            if (hist_index + 1 < static_cast<int>(hist_list.size())) {
                buf = hist_list[++hist_index];
            }
        } else {
            if (hist_index > 0) {
                buf = hist_list[--hist_index];
            } else {
                buf = temp_buf;
                hist_index = -1;
            }
        }
        edit_pos = buf.size();
    }

    // input_loop
    void process_input() {
        buf.clear();
        edit_pos = 0, hist_index = -1;
        last_key_was_tab = false;
        bool brk = false;
        while (!brk) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) <= 0) {
                return;
            }
            if (ch != '\t') {
                last_key_was_tab = false;
            }
            switch (ch) {
            case 0x7F: // Backspace
            case '\b':
                handle_backspace();
                break;
            case 0x17: // CTRL+W (向左删除单词)
                handle_delete_word_left();
                break;
            case 0x1B: // ESC
                handle_escape_sequence();
                break;
            case '\n' /* | '\r'*/: // Enter
                handle_commit();
                brk = true;
                break;
            case '\t': // Tab键
                handle_tab_completion();
                break;
            default:
                if (isprint(ch)) {
                    buf.insert(edit_pos, 1, ch);
                    ++edit_pos;
                    redisplay();
                }
            }
        }
    }

    // prompt
    void update_prompt() {
        char cwd[256];
        if (getcwd(reinterpret_cast<char*>(cwd), sizeof(cwd))) {
            current_prompt = reinterpret_cast<char*>(cwd);
        } else {
            current_prompt = "? > ";
        }
    }
    void print_prompt() {
        std::cout << "\033[38;2;102;204;255m" << current_prompt << "\033[0m > ";
        std::cout.flush();
    }
    // refresh_line
    void redisplay() {
        size_t total_pos = current_prompt.size() + 3 + edit_pos;
        std::cout << "\033[2K\r";
        print_prompt();
        std::cout << buf << "\r\033[" << total_pos << "C";
        std::cout.flush();
    }

    // handle
    static void handle_cd(const std::vector<std::string>& args) {
        std::string path = args.size() > 1 ? args[1] : std::getenv("HOME");
        if (chdir(path.c_str()) != 0) {
            perror("cd failed");
        }
    }
    void handle_backspace() {
        if (edit_pos > 0 && edit_pos <= buf.size()) {
            buf.erase(edit_pos - 1, 1);
            --edit_pos;
        }
        redisplay();
    }
    void handle_escape_sequence() {
        char seq[5];
        if (read(STDIN_FILENO, &seq[0], 1) <= 0)
            return;
        if (seq[0] == '[') {
            if (read(STDIN_FILENO, &seq[1], 1) <= 0)
                return;
            if (seq[1] >= 'A' && seq[1] <= 'D') { // 简单箭头键
                switch (seq[1]) {
                case 'A': // 上键
                    navigate_history(true);
                    break;
                case 'B': // 下键
                    navigate_history(false);
                    break;
                case 'C': // 右键
                    if (edit_pos < buf.size()) {
                        edit_pos++;
                    }
                    break;
                case 'D': // 左键
                    if (edit_pos > 0) {
                        edit_pos--;
                    }
                    break;
                }
            } else if (seq[1] == '1') { // 可能是带修饰符的键
                if (read(STDIN_FILENO, &seq[2], 3) == 3 && seq[2] == ';' && seq[3] == '5') {
                    if (seq[4] == 'C') {
                        handle_ctrl_right_arrow();
                        return;
                    } else if (seq[4] == 'D') {
                        handle_ctrl_left_arrow();
                        return;
                    }
                }
            } else if (seq[1] == '3') { // Delete键
                if (read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == '~') {
                    if (edit_pos < buf.size()) {
                        buf.erase(edit_pos, 1);
                    }
                }
            }
        } else if (seq[0] == 'd') { // 带修饰符的Delete
            handle_delete_word_right();
            return;
        }
        redisplay();
    }
    void handle_commit() {
        if (!buf.empty()) {
            history.add_command(buf, current_prompt);
        }
    }
    // 查找最长公共前缀
    static std::string find_longest_common_prefix(const std::vector<std::string>& strings) {
        if (strings.empty())
            return "";
        std::string prefix = strings[0];
        for (size_t i = 1; i < strings.size(); ++i) {
            while (strings[i].rfind(prefix, 0) != 0) {
                prefix = prefix.substr(0, prefix.length() - 1);
                if (prefix.empty())
                    return "";
            }
        }
        return prefix;
    }
    void handle_tab_completion() {
        size_t cursor_pos = edit_pos;
        size_t word_start = buf.rfind(' ', cursor_pos - 1);
        word_start = (word_start == std::string::npos) ? 0 : word_start + 1;
        std::string current_word = buf.substr(word_start, cursor_pos - word_start);

        std::vector<std::string> matches;
        bool is_command = (buf.find(' ') == std::string::npos);
        std::string dir_path = ".";
        if (is_command) {
            // 静态补全: 内建命令
            if (std::string("cd").rfind(current_word, 0) == 0)
                matches.push_back("cd");
            if (std::string("exit").rfind(current_word, 0) == 0)
                matches.push_back("exit");
            // 动态补全: PATH中的可执行文件
            for (const auto& dir : path_dirs) {
                try {
                    for (const auto& entry : fs::directory_iterator(dir)) {
                        if (entry.is_regular_file() || entry.is_symlink()) {
                            std::string filename = entry.path().filename().string();
                            if (filename.rfind(current_word, 0) == 0) {
                                if ((entry.status().permissions() & fs::perms::owner_exec) != fs::perms::none) {
                                    if (std::find(matches.begin(), matches.end(), filename) == matches.end()) {
                                        matches.push_back(filename);
                                    }
                                }
                            }
                        }
                    }
                } catch (const fs::filesystem_error& e) { /* 忽略无法访问的目录 */
                }
            }
        } else {
            // 动态补全: 文件或目录路径
            std::string base_name = current_word;
            size_t slash_pos = current_word.rfind('/');
            if (slash_pos != std::string::npos) {
                dir_path = current_word.substr(0, slash_pos + 1);
                base_name = current_word.substr(slash_pos + 1);
            }
            try {
                for (const auto& entry : fs::directory_iterator(dir_path)) {
                    std::string filename = entry.path().filename().string();
                    if (filename.rfind(base_name, 0) == 0) {
                        matches.push_back(filename);
                    }
                }
            } catch (const fs::filesystem_error& e) { /* 忽略无效路径 */
            }
        }

        if (matches.empty()) {
            redisplay();
            return; // 没有匹配项
        }
        if (matches.size() == 1) {
            // 唯一匹配
            std::string completion = (dir_path == "." ? "" : dir_path) + matches[0];
            if (fs::is_directory(completion)) {
                completion += "/";
            } else {
                completion += " ";
            }
            buf.replace(word_start, current_word.length(), completion);
            edit_pos = word_start + completion.length();
        } else {
            // 多重匹配
            std::string common_prefix;
            // 提取相对于补全开始点的真实前缀
            size_t slash_pos = current_word.rfind('/');
            if (slash_pos != std::string::npos) {
                common_prefix = current_word.substr(0, slash_pos + 1);
            }
            common_prefix += find_longest_common_prefix(matches);
            buf.replace(word_start, current_word.length(), common_prefix);
            edit_pos = word_start + common_prefix.length();

            // 如果是第二次按Tab，则显示所有选项
            if (last_key_was_tab) {
                std::cout << "\n";
                for (const auto& match : matches) {
                    std::cout << fs::path(match).filename().string() << "\t";
                }
                std::cout << "\n";
            }
        }
        last_key_was_tab = true;
        redisplay();
    }
    void handle_ctrl_right_arrow() {
        while (edit_pos < buf.length() && !std::isspace(buf[edit_pos])) {
            edit_pos++;
        }
        while (edit_pos < buf.length() && std::isspace(buf[edit_pos])) {
            edit_pos++;
        }
        redisplay();
    }
    void handle_ctrl_left_arrow() {
        // 如果光标不在开头
        if (edit_pos > 0) {
            // 先向左移动一格，以确保我们处理的是光标前的字符
            edit_pos--;
        }
        while (edit_pos > 0 && std::isspace(buf[edit_pos])) {
            edit_pos--;
        }
        while (edit_pos > 0 && !std::isspace(buf[edit_pos])) {
            edit_pos--;
        }
        // 如果我们停在了一个空格上（意味着我们越过了一个单词），向右移动一格到单词的开头
        if (edit_pos > 0 && std::isspace(buf[edit_pos])) {
            edit_pos++;
        }
        redisplay();
    }
    void handle_delete_word_left() {
        if (edit_pos == 0) {
            redisplay();
            return;
        }
        size_t original_pos = edit_pos;
        edit_pos--;
        while (edit_pos > 0 && std::isspace(buf[edit_pos])) {
            edit_pos--;
        }
        while (edit_pos > 0 && !std::isspace(buf[edit_pos - 1])) {
            edit_pos--;
        }
        buf.erase(edit_pos, original_pos - edit_pos);
        redisplay();
    }
    void handle_delete_word_right() {
        if (edit_pos >= buf.length()) {
            redisplay();
            return;
        }
        size_t original_pos = edit_pos;
        size_t delete_end = edit_pos;
        while (delete_end < buf.length() && !std::isspace(buf[delete_end])) {
            delete_end++;
        }
        while (delete_end < buf.length() && std::isspace(buf[delete_end])) {
            delete_end++;
        }
        buf.erase(original_pos, delete_end - original_pos);
        redisplay();
    }

    // main_loop
    void main_loop() {
        while (true) {
            enable_raw_mode();
            update_prompt(), print_prompt();

            process_input();
            try {
                auto cmd = parse_command(buf);
                if (cmd)
                    cmd->execute();
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << '\n';
            }
            disable_raw_mode();
        }
    }

    // 命令解析器逻辑
    std::unique_ptr<Command> parse_command(const std::string& input) {
        // 特判（）
        if (input.front() == '(' && input.back() == ')') {
            return parse_command(input.substr(1, input.size() - 2));
        }
        // 分割管道符号
        size_t pipe_pos = std::string::npos;
        int bracket_depth = 0; // 支持未来括号嵌套
        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];
            if (c == '\\' && i + 1 < input.size()) { // 跳过转义字符
                ++i;
                continue;
            }
            if (c == '(')
                ++bracket_depth;
            if (c == ')')
                --bracket_depth;
            if (bracket_depth == 0 && c == '|') {
                pipe_pos = i;
                break; // 找到最左端顶层管道
            }
        }

        if (pipe_pos != std::string::npos) {
            auto cmd = std::make_unique<PipeCommand>();
            cmd->left = parse_single_command(trim(input.substr(0, pipe_pos)));
            cmd->right = parse_command(trim(input.substr(pipe_pos + 1)));
            return cmd;
        }
        return parse_single_command(input);
    }

    std::unique_ptr<Command> parse_single_command(const std::string& cmd_str) {
        auto tokens = tokenize(cmd_str);
        std::vector<std::string> args;
        std::string input_file;
        std::string output_file;
        bool append = false;

        // 遍历词元处理重定向
        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto& token = tokens[i];

            if (token.type == TokenType::REDIRECT_IN) {
                if (++i >= tokens.size() || tokens[i].type != TokenType::ARG) {
                    throw std::invalid_argument("输入重定向缺少文件名");
                }
                input_file = tokens[i].value;
            } else if (token.type == TokenType::REDIRECT_OUT) {
                if (++i >= tokens.size() || tokens[i].type != TokenType::ARG) {
                    throw std::invalid_argument("输出重定向缺少文件名");
                }
                output_file = tokens[i].value;
            } else if (token.type == TokenType::REDIRECT_APPEND) {
                if (++i >= tokens.size() || tokens[i].type != TokenType::ARG) {
                    throw std::invalid_argument("追加输出缺少文件名");
                }
                output_file = tokens[i].value;
                append = true;
            } else {
                args.push_back(token.value);
            }
        }
        // typedef void (*CallbackFunction)(Shell*, std::vector<std::string>&);
        auto exec_cmd =
            std::make_unique<ExecCommand>(args, [this](std::vector<std::string>& args) { execute_command(args); });

        if (!input_file.empty() || !output_file.empty()) {
            auto redir_cmd = std::make_unique<RedirCommand>();
            redir_cmd->child = std::move(exec_cmd);
            redir_cmd->input_file = input_file;
            redir_cmd->output_file = output_file;
            redir_cmd->append = append;
            return redir_cmd;
        }

        return exec_cmd;
    }

    // 辅助函数：去除字符串两端空白
    static std::string trim(const std::string& s) {
        auto start = s.begin();
        while (start != s.end() && std::isspace(*start)) {
            ++start;
        }
        auto end = s.end();
        while (end != start && std::isspace(*(end - 1))) {
            --end;
        }
        return {start, end};
    }
};

Shell* Shell::instance = nullptr;

void Shell::sigint_handler(int sig) {
    if (instance) {
        switch (sig) {
        case SIGINT:
            std::cout << "\n";
            instance->buf.clear();
            instance->edit_pos = 0;
            instance->redisplay();
            break;
        }
    }
}

int main() {
    HistoryManager hist;
    Shell shell(hist);
    Shell::instance = &shell;
    signal(SIGINT, Shell::sigint_handler);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    shell.run();
    Shell::instance = nullptr;
    return 0;
}