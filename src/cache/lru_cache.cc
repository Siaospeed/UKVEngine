#include "lru_cache.h"

#include <iostream>

LruCache::LruCache(size_t capacity)
        : capacity_(capacity), size_(0) {
    head_ = new Node();
    tail_ = new Node();

    head_->next = tail_;
    tail_->prev = head_;
}

LruCache::~LruCache() {
    Node* current = head_;
    while (current != nullptr) {
        Node* next_node = current->next;
        delete current;
        current = next_node;
    }
}

bool LruCache::Get(const std::string& key, std::string* out_value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (table_.find(key) == table_.end()) {
        return false;
    }
    Node* node = table_[key];
    MoveToHead(node);
    if (out_value) {
        *out_value = node->value;
    }
    return true;
}

void LruCache::Put(std::string key, std::string value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (table_.find(key) != table_.end()) {
        Node* node = table_[key];
        node->value = std::move(value);
        MoveToHead(node);
    } else {
        Node* new_node = new Node(std::move(key), std::move(value));
        table_.try_emplace(new_node->key, new_node);

        AttachToHead(new_node);
        size_++;

        if (size_ > capacity_) {
            RemoveLruItem();
        }
    }
}

bool LruCache::Delete(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (table_.find(key) == table_.end()) {
        return false;
    }

    Node* node = table_[key];
    DetachNode(node);
    table_.erase(key);
    size_--;
    delete node;

    return true;
}

[[deprecated]] void LruCache::DebugDisplay() const {
    if (size_ == 0) {
        std::cout << "    Cache: (Empty)" << std::endl;
        return;
    }

    Node* curr = head_->next;
    std::cout << "    Cache: [HOT] ";
    while (curr != tail_) {
        std::cout << "(" << curr->key << ":" << curr->value << ")";
        if (curr->next != tail_) {
            std::cout << " <-> ";
        }
        curr = curr->next;
    }
    std::cout << " [COLD]" << std::endl;
}
