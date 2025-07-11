#include <algorithm>
#include <cctype>
#include <clocale>
#include <filesystem>
#include <iostream>
#include <locale>
#include <string>
#include <vector>

#define COLOR_DIR   "\033[1;34m" // 目录      （粗体蓝色）
#define COLOR_FILE  "\033[0m"    // 文件      （默认）
#define COLOR_EXE   "\033[1;32m" // 可执行文件（粗体绿色）
#define COLOR_LINK  "\033[1;36m" // 符号链接  （粗体青色）
#define COLOR_RESET "\033[0m"    // 重置颜色

namespace fs = std::filesystem;

struct Config {
    int max_level = 1;
    bool show_color = false;
    bool show_error = true;
};

// 统计结构
struct Stats {
    size_t dirs = 0;
    size_t files = 0;
} stats;

std::string get_color(const fs::path& path) {
    try {
        if (fs::is_symlink(path))
            return COLOR_LINK;
        else if (fs::is_directory(path))
            return COLOR_DIR;
        else if (fs::is_regular_file(path)) {
#ifndef _WIN32
            auto perms = fs::status(path).permissions();
            if ((perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) != fs::perms::none) {
                return COLOR_EXE;
            }
#endif
            return COLOR_EXE;
        }
    } catch (...) {
    }
    return COLOR_FILE;
}

void print_tree(const fs::path& path, const Config& config, int current_level = 0, const std::string& prefix = "") {
    if (current_level > config.max_level) {
        return;
    }
    try {
        std::vector<fs::directory_entry> entries;
        for (const auto& entry : fs::directory_iterator(path)) {
            std::string filename = entry.path().filename().string();
            if (filename == "." || filename == "..") {
                continue;
            }
            entries.push_back(entry);
        }
        // 排序：目录在前，文件在后，按字母序
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            bool a_is_dir = fs::is_directory(a.status());
            bool b_is_dir = fs::is_directory(b.status());
            if (a_is_dir != b_is_dir) {
                return a_is_dir;
            }
            return a.path().filename() < b.path().filename();
        });

        size_t count = entries.size();
        for (size_t i = 0; i < count; ++i) {
            const auto& entry = entries[i];
            std::string name = entry.path().filename().string();
            bool is_last = (i == count - 1);
            if (current_level > 0) {
                std::cout << prefix;
                std::cout << (is_last ? "└── " : "├── ");
            }
            if (config.show_color) {
                std::string color = get_color(entry.path());
                std::cout << color << name << COLOR_RESET;
            } else {
                std::cout << name;
            }
            if (fs::is_directory(entry.status())) {
                std::cout << "/"; // 目录标记
                stats.dirs++;
                std::cout << std::endl;
                if (current_level < config.max_level) {
                    std::string new_prefix = prefix + (is_last ? "    " : "│   ");
                    print_tree(entry, config, current_level + 1, new_prefix);
                }
            } else if (fs::is_symlink(entry.status())) {
                std::cout << " -> ";
                try {
                    auto target = fs::read_symlink(entry.path());
                    if (config.show_color) {
                        std::string color = get_color(target);
                        std::cout << color << target.string() << COLOR_RESET;
                    } else {
                        std::cout << target.string();
                    }
                } catch (const fs::filesystem_error&) {
                    std::cout << "[broken]";
                }
                stats.files++;
                std::cout << std::endl;
            } else {
                stats.files++;
                std::cout << std::endl;
            }
        }
    } catch (const fs::filesystem_error& e) {
        if (config.show_error) {
            std::cerr << COLOR_RESET << prefix << "└── [Error: " << e.what() << "]" << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    // 设置 UTF-8 输出
    std::setlocale(LC_ALL, "en_US.UTF-8");
    std::locale::global(std::locale(""));

    Config config;
    fs::path path = ".";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-L" && i + 1 < argc) {
            config.max_level = std::max(1, std::stoi(argv[++i]));
        } else if (arg == "-C") {
            config.show_color = true;
        } else {
            path = arg;
        }
    }

    if (fs::is_directory(path)) {
        stats.dirs++;
    }
    std::cout << "." << std::endl;
    print_tree(path, config, 1);

    std::string dir_label = (stats.dirs == 1) ? "directory" : "directories";
    std::string file_label = (stats.files == 1) ? "file" : "files";
    std::cout << "\n" << stats.dirs << " " << dir_label << ", " << stats.files << " " << file_label << std::endl;

    return 0;
}