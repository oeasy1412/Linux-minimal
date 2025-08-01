#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <getopt.h>
#include <string>
#include <unistd.h>
#include <vector>

// 空白字符
inline bool is_space(unsigned char c) {
    return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

bool count_wc(FILE* fp, unsigned long& lines, unsigned long& words, unsigned long& bytes) {
    lines = words = bytes = 0;
    bool in_word = false;
    char buffer[4096];

    while (size_t nread = fread(buffer, 1, sizeof(buffer), fp)) {
        bytes += nread;
        for (size_t i = 0; i < nread; ++i) {
            unsigned char c = buffer[i];
            if (c == '\n')
                ++lines;
            if (is_space(c)) {
                in_word = false;
            } else if (c <= 0x1F || (c >= 0x7F && c <= 0x9F)) {
            } else if ((c >= 0x21 && c <= 0x7E)) {
                if (!in_word) {
                    in_word = true;
                    ++words;
                }
            }
        }
    }

    if (ferror(fp)) {
        perror("Read error");
        return false;
    }
    return true;
}

void print_help(const char* program_name) {
    printf(
        "Usage: %s [OPTION] [FILE]...\n\n"
        "Count lines, words, and bytes for each FILE (or stdin)\n\n"
        "  -l, --lines            print the newline counts\n"
        "  -w, --words            print the word counts\n"
        "  -c, --bytes            print the byte counts\n"
        "      --help             display this help and exit\n\n",
        program_name);
}

int main(int argc, char* argv[]) {
    bool opt_l = false, opt_w = false, opt_c = false;
    static struct option longopts[] = {{"lines", no_argument, 0, 'l'},
                                       {"words", no_argument, 0, 'w'},
                                       {"bytes", no_argument, 0, 'c'},
                                       {"help", no_argument, 0, 'h'},
                                       {0, 0, 0, 0}};
    int opt, longind;
    while ((opt = getopt_long(argc, argv, "lwc", longopts, &longind)) != -1) {
        switch (opt) {
        case 'l':
            opt_l = true;
            break;
        case 'w':
            opt_w = true;
            break;
        case 'c':
            opt_c = true;
            break;
        default:
            print_help(argv[0]);
            return 1;
        }
    }
    // 默认无选项时显示所有统计
    bool show_all = !(opt_l || opt_w || opt_c);
    bool show_lines = show_all || opt_l;
    bool show_words = show_all || opt_w;
    bool show_bytes = show_all || opt_c;

    std::vector<std::string> files;
    for (int i = optind; i < argc; ++i) {
        files.push_back(argv[i]);
    }
    if (files.empty()) {
        files.push_back("-");
    }
    // 处理所有文件
    unsigned long total_lines = 0, total_words = 0, total_bytes = 0;
    for (const auto& fname : files) {
        FILE* fp = stdin;
        if (fname != "-") {
            fp = fopen(fname.c_str(), "rb");
            if (!fp) {
                perror(fname.c_str());
                continue;
            }
        }
        unsigned long lines = 0, words = 0, bytes = 0;
        bool success = count_wc(fp, lines, words, bytes);
        if (fp != stdin)
            fclose(fp);
        if (!success)
            continue;
        // 累加总计
        total_lines += lines;
        total_words += words;
        total_bytes += bytes;

        // 格式化输出
        char buf[256] = {0};
        char* ptr = buf;
        if (show_lines)
            ptr += sprintf(ptr, " %8lu", lines);
        if (show_words)
            ptr += sprintf(ptr, " %9lu", words);
        if (show_bytes)
            ptr += sprintf(ptr, " %9lu", bytes);
        // 附加文件名（标准输入不显示名称）
        if (fname == "-" && files.size() == 1) {
            *ptr = '\0'; // 无文件名显示
        } else {
            sprintf(ptr, " %s", fname.c_str());
        }

        puts(buf);
    }

    // 多文件显示总计
    if (files.size() > 1 || (files.size() == 1 && files[0] == "-")) {
        if (show_lines)
            printf(" %8lu", total_lines);
        if (show_words)
            printf(" %9lu", total_words);
        if (show_bytes)
            printf(" %9lu", total_bytes);
        printf(" total\n");
    }

    return 0;
}