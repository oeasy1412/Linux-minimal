#ifndef __HISTORYMAMAGER_H__
#define __HISTORYMAMAGER_H__

#include <algorithm>
#include <chrono>
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
  private:
    bool running = true;
    std::mutex mtx;
    std::thread save_thread;

  public:
    HistoryManager(const std::string& path = default_save_path()) : save_path(path) {
        // load(save_path); // 初始化时加载历史记录
        save();
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
    void add_command(const std::string& cmd, const std::string& cwd) {
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
        Node* new_node = new Node(HistoryItem(cmd, cmd));
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
    std::vector<std::string> get_history(size_t limit = 10) const {
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
    // 原子化保存（使用临时文件+重命名）
    void save() {
        std::lock_guard<std::mutex> lock(mtx);
        std::string tmp_path = save_path + ".tmp";

        try {
            std::ofstream ofs(tmp_path, std::ios::binary);
            if (!ofs)
                throw std::runtime_error("无法打开临时文件");

            for (const auto& node : lru_list) {
                // 转义特殊字符
                const HistoryItem& entry = node->item;
                std::string escaped_cmd = entry.command;
                std::string usage = std::to_string(entry.usage_count);

                ofs << std::left << std::setw(20) << escaped_cmd << "  Used:" << usage << "  "
                    << (std::chrono::system_clock::to_time_t(entry.timestamp)) << "\n";
            }

            ofs.flush();
            if (!ofs)
                throw std::runtime_error("写入文件失败");

                // 原子替换原文件
#ifdef _WIN32
            std::remove(save_path.c_str());
#endif
            if (std::rename(tmp_path.c_str(), save_path.c_str()) != 0) {
                throw std::runtime_error("重命名失败");
            }
        } catch (const std::exception& e) {
            std::cerr << "保存失败: " << e.what() << std::endl;
            std::remove(tmp_path.c_str());
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
            return std::string(path) + "\\mysh_history";
        }
        return "mysh_history";
#else
        return std::string(getenv("HOME")) + "/mysh_history";
#endif
    }
    //   private:
    //     json serialize() const {
    //         json j;
    //         json entries = json::array();

    //         Node* current = head;
    //         while (current) {
    //             json entry;
    //             entry["command"] = current->item.command;
    //             entry["paths"] = current->item.paths;
    //             entry["timestamp"] = current->item.timestamp;
    //             entries.push_back(entry);
    //             current = current->next;
    //         }

    //         j["entries"] = entries;
    //         return j;
    //     }
};

#endif // __HISTORYMAMAGER_H__