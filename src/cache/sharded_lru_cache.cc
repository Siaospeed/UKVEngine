#include "sharded_lru_cache.h"

#include <cassert>

ShardedLruCache::ShardedLruCache(size_t capacity, size_t num_shards)
        : num_shards_(num_shards) {
    assert((num_shards & (num_shards - 1)) == 0 && "num_shards must be power of 2");
    for (size_t i = 0; i < num_shards_; i++) {
        shards_.push_back(std::make_unique<LruCache>(capacity / num_shards_));
    }
}

bool ShardedLruCache::Get(const std::string& key, std::string* value) {
    size_t hash_val = std::hash<std::string>{}(key);
    size_t shard_index = hash_val & (num_shards_ - 1);

    return shards_[shard_index]->Get(key, value);
}

void ShardedLruCache::Put(std::string key, std::string value) {
    size_t hash_val = std::hash<std::string>{}(key);
    size_t shard_index = hash_val & (num_shards_ - 1);

    shards_[shard_index]->Put(std::move(key), std::move(value));
}

bool ShardedLruCache::Delete(const std::string& key) {
    size_t hash_val = std::hash<std::string>{}(key);
    size_t shard_index = hash_val & (num_shards_ - 1);

    return shards_[shard_index]->Delete(key);
}
