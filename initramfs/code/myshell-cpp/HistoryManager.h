#ifndef __HISTORYMAMAGER_H__
#define __HISTORYMAMAGER_H__

#include <algorithm>
#include <chrono>
#include <iostream>
#include <list>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

// 历史记录条目
struct HistoryItem {
    std::string command;
    std::vector<std::string> paths;
    std::chrono::system_clock::time_point timestamp;
    size_t usage_count = 1;

    HistoryItem(std::string cmd, std::vector<std::string> p = {})
        : command(std::move(cmd)),
          paths(std::move(p)),
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

    Node* head = nullptr; // 链表头（最新记录）
    Node* tail = nullptr; // 链表尾（最旧记录）

    std::unordered_map<std::string, Node*> cmd_map; // command-Node 快速查找map
    std::list<Node*> lru_list;                      // LRU淘汰队列

  public:
    ~HistoryManager() {
        Node* current = head;
        while (current) {
            Node* next = current->next;
            delete current;
            current = next;
        }
    }

    // 添加历史记录
    void add_command(const std::string& cmd, const std::vector<std::string>& paths = {}) {
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
        Node* new_node = new Node(HistoryItem(cmd, paths));
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
        Node* current = head;
        while (current && limit-- > 0) {
            result.push_back(current->item.command);
            current = current->next;
        }
        return result;
    }

    // 上下文感知获取历史记录
    std::vector<std::string> get_context_history(const std::vector<std::string>& current_paths) const {
        std::vector<std::string> result;
        Node* current = head;
        while (current) {
            if (has_common_paths(current->item.paths, current_paths)) {
                result.push_back(current->item.command);
            }
            current = current->next;
        }
        return result;
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
};

#endif // __HISTORYMAMAGER_H__