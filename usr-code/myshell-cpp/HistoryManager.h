#ifndef __HISTORYMAMAGER_H__
#define __HISTORYMAMAGER_H__

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <mutex>
#include <string>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

// 历史记录条目
struct HistoryItem {
    std::string command;
    std::string directory;
    std::chrono::system_clock::time_point timestamp;
    size_t usage_count = 1;

    HistoryItem(const std::string& cmd, const std::string& dir)
        : command(cmd),
          directory(dir),
          timestamp(std::chrono::system_clock::now()) {}

    static HistoryItem deserialize(const std::string& line) {
        // 解析格式: [命令] Dir:[目录] Used:[使用次数] [时间戳]
        size_t dir_pos = line.find("Dir:");
        size_t used_pos = line.find("Used:");
        size_t ts_pos = line.find_last_of(" ");

        if (dir_pos == std::string::npos || used_pos == std::string::npos || ts_pos == std::string::npos) {
            throw std::runtime_error("Invalid history format");
        }
        std::string command = line.substr(0, dir_pos - 1);
        size_t end_cmd = command.find_last_not_of(" ");
        if (end_cmd != std::string::npos) {
            command = command.substr(0, end_cmd + 1);
        }
        std::string directory = line.substr(dir_pos + 4, used_pos - (dir_pos + 4));
        size_t end_dir = directory.find_last_not_of(" ");
        if (end_dir != std::string::npos) {
            directory = directory.substr(0, end_dir + 1);
        }
        std::string usage_str = line.substr(used_pos + 5, ts_pos - (used_pos + 5));
        size_t usage_count = std::stoul(usage_str);
        std::string ts_str = line.substr(ts_pos + 1);
        time_t timestamp = std::stol(ts_str);
        HistoryItem item(command, directory);
        item.usage_count = usage_count;
        item.timestamp = std::chrono::system_clock::from_time_t(timestamp);
        return item;
    }
};

// 历史记录管理类
class HistoryManager {
  public:
    // 双向链表节点
    struct Node {
        HistoryItem item;
        Node* prev = nullptr;
        Node* next = nullptr;
        typename std::list<Node*>::iterator lru_it;

        Node(HistoryItem it) : item(std::move(it)) {}
    };

  private:
    static constexpr size_t MAX_HISTORY = 500;
    const std::string save_path;

    Node* head = nullptr; // 链表头（最新记录）
    Node* tail = nullptr; // 链表尾（最旧记录）

    std::unordered_map<std::string, Node*> cmd_map; // command-Node 快速查找map
    std::list<Node*> lru_list;                      // LRU淘汰队列
    std::chrono::system_clock::time_point last_save_time;

  private:
    bool running = true;
    std::mutex mtx;
    std::thread save_thread;

  public:
    HistoryManager(const std::string& path = default_save_path())
        : save_path(path),
          last_save_time(std::chrono::system_clock::now()) {
        save();
        load();
        running = true;
        start_auto_save();
    }
    ~HistoryManager() {
        save();
        Node* current = head;
        while (current) {
            Node* next = current->next;
            delete current;
            current = next;
        }
    }

    // 添加历史记录
    // void add_command_by
    inline void add_command(const std::string& cmd, const std::string& cwd = "/") {
        add_itemcommand(HistoryItem(cmd, cwd));
    }
    void add_itemcommand(const HistoryItem& item) {
        const std::string &cmd = item.command, cwd = item.directory;
        if (cmd.empty()) {
            return;
        }
        // 相同检查并更新
        if (auto it = cmd_map.find(cmd); it != cmd_map.end()) {
            Node* node = it->second;
            node->item.usage_count++;
            touch_node(node);
            return;
        }

        // 创建新节点
        Node* new_node = new Node(item);
        // 插入链表头部
        if (!head) {
            head = tail = new_node;
        } else {
            new_node->next = head;
            head->prev = new_node;
            head = new_node;
        }
        // 更新辅助结构
        cmd_map[cmd] = new_node;
        lru_list.push_front(new_node);
        new_node->lru_it = lru_list.begin();

        // 执行淘汰策略
        if (cmd_map.size() > MAX_HISTORY) {
            evict_oldest();
        }
    }

    // 获取历史记录（按时间倒序）
    std::vector<std::string> get_history(size_t limit = MAX_HISTORY / 5) const {
        std::vector<std::string> result;
        result.reserve(limit);
        for (auto node = lru_list.begin(); node != lru_list.end() && limit-- > 0; ++node) {
            result.push_back((*node)->item.command);
        }
        return result;
    }

    // 上下文感知获取历史记录
    // std::vector<std::string> get_context_history(const std::vector<std::string>& current_paths) const {
    //     std::vector<std::string> result;
    //     Node* current = head;
    //     while (current) {
    //         if (has_common_paths(current->item.paths, current_paths)) {
    //             result.push_back(current->item.command);
    //         }
    //         current = current->next;
    //     }
    //     return result;
    // }

  private:
    void start_auto_save() {
        save_thread = std::thread([this] {
            while (running) {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                // std::cout << "save\n";
                save();
            }
        });
    }
    // 原子化保存
    void save() {
        std::lock_guard<std::mutex> lock(mtx);
        try {
            // 使用低级fd确保原子写入
            int fd = open(save_path.c_str(), O_WRONLY | O_CREAT , 0644);
            if (fd == -1) {
                if (errno != ENOENT) { // 文件不存在不是错误
                    throw std::runtime_error("无法打开临时文件: " + std::string(strerror(errno)));
                }
                return;
            }

            for (const auto& node : lru_list) {
                const HistoryItem& entry = node->item;
                // if (entry.timestamp < last_save_time) {
                //     continue;
                // }
                std::string escaped_cmd = entry.command;
                std::string usage = std::to_string(entry.usage_count);

                std::ostringstream oss;
                oss << std::left << std::setw(20) << entry.command << "  Used:" << entry.usage_count << "  "
                    << std::chrono::system_clock::to_time_t(entry.timestamp) << "\n";
                last_save_time = entry.timestamp;
                std::string line = oss.str();

                if (write(fd, line.c_str(), line.size()) != static_cast<ssize_t>(line.size())) {
                    close(fd);
                    throw std::runtime_error("写入文件失败: " + std::string(strerror(errno)));
                }
            }

            // 强制数据持久化到磁盘
            if (fsync(fd) != 0) {
                close(fd);
                throw std::runtime_error("fsync失败: " + std::string(strerror(errno)));
            }
            close(fd);
        } catch (const std::exception& e) {
            std::cerr << "保存失败: " << e.what() << std::endl;
            std::remove(save_path.c_str());
        }
    }

    void load() {
        std::lock_guard<std::mutex> lock(mtx);
        try {
            std::ifstream ifs(save_path);
            if (!ifs.is_open()) {
                throw std::runtime_error("无法打开历史记录文件: " + save_path);
            }
            // 逐行读取
            std::string line;
            int line_count = 0;
            while (std::getline(ifs, line)) {
                line_count++;
                try {
                    if (line.empty()) {
                        continue;
                    }
                    // 解析历史记录
                    size_t used_pos = line.find("Used:");
                    size_t ts_pos = line.find_last_of(" ");
                    if (used_pos == std::string::npos || ts_pos == std::string::npos) {
                        throw std::runtime_error("无效的历史记录格式");
                    }
                    // 提取记录
                    std::string command = line.substr(0, used_pos - 2);
                    size_t end_cmd = command.find_last_not_of(" ");
                    if (end_cmd != std::string::npos) {
                        command = command.substr(0, end_cmd + 1);
                    }
                    std::string usage_str = line.substr(used_pos + 5, ts_pos - (used_pos + 5));
                    int usage_count = std::stoi(usage_str);
                    std::string ts_str = line.substr(ts_pos + 1);
                    time_t timestamp = std::stol(ts_str);
                    // 添加到历史记录
                    HistoryItem item(command, "/");
                    item.usage_count = usage_count;
                    item.timestamp = std::chrono::system_clock::from_time_t(timestamp);
                    add_itemcommand(item);
                } catch (const std::exception& e) {
                    std::cerr << "解析历史记录行 " << line_count << " 失败: " << e.what() << "\n行内容: " << line
                              << std::endl;
                }
            }
            lru_list.reverse();
        } catch (const std::exception& e) {
            std::cerr << "加载历史记录失败: " << e.what() << std::endl;
        }
    }

  private:
    // 更新节点访问状态
    void touch_node(Node* node) { lru_list.splice(lru_list.begin(), lru_list, node->lru_it); }

    // 淘汰最旧记录
    void evict_oldest() {
        Node* node = lru_list.back();
        // 从链表移除
        if (node->prev) {
            node->prev->next = node->next;
        }
        if (node->next) {
            node->next->prev = node->prev;
        }
        if (node == head) {
            head = node->next;
        }
        if (node == tail) {
            tail = node->prev;
        }
        // 清理辅助结构
        cmd_map.erase(node->item.command);
        lru_list.pop_back();
        delete node;
    }

    // 路径匹配检查
    static bool has_common_paths(const std::vector<std::string>& a, const std::vector<std::string>& b) {
        return std::any_of(
            a.begin(), a.end(), [&](const auto& path) { return std::find(b.begin(), b.end(), path) != b.end(); });
    }

  private:
    static std::string default_save_path() {
#ifdef _WIN32
        char path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
            return std::string(path) + "\\.mysh_history";
        }
        return ".mysh_history";
#else
        return std::string(getenv("HOME")) + "/.mysh_history";
#endif
    }
};

#endif // __HISTORYMAMAGER_H__