#ifndef UKVENGINE_LRU_CACHE_H_
#define UKVENGINE_LRU_CACHE_H_

#include <mutex>
#include <string>
#include <unordered_map>

struct Node {
    std::string key;
    std::string value;
    Node* prev;
    Node* next;

    Node() : prev(nullptr), next(nullptr) {}

    Node(std::string k, std::string v)
        : key(std::move(k)), value(std::move(v)), prev(nullptr), next(nullptr) {}
};

class LruCache {
public:
    explicit LruCache(size_t capacity);

    LruCache(const LruCache&) = delete;
    LruCache& operator=(const LruCache&) = delete;

    ~LruCache();

    bool Get(const std::string& key, std::string* out_value);
    void Put(std::string key, std::string value);
    bool Delete(const std::string& key);

private:
    size_t capacity_;
    size_t size_;
    Node* head_;
    Node* tail_;
    std::unordered_map<std::string, Node*> table_;

    std::mutex mutex_;

    void DetachNode(Node* node) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }

    void AttachToHead(Node* node) {
        node->prev = head_;
        node->next = head_->next;
        head_->next->prev = node;
        head_->next = node;
    }

    void MoveToHead(Node* node) {
        DetachNode(node);
        AttachToHead(node);
    }

    void RemoveLruItem() {
        if (size_ == 0) return;
        Node* lru_node = tail_->prev;
        DetachNode(lru_node);
        table_.erase(lru_node->key);
        delete lru_node;
        size_--;
    }
};

#endif // !UKVENGINE_LRU_CACHE_H_
