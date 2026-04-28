#include "sharded_lru_cache.h"

ShardedLruCache::ShardedLruCache(size_t capacity, size_t num_shards) {
    for (size_t i = 0; i < num_shards; i++) {
        shards_.push_back(std::make_unique<LruCache>(capacity / num_shards));
    }
}

bool ShardedLruCache::Get(const std::string& key, std::string* value) {
    size_t hash_val = std::hash<std::string>{}(key);
    size_t shard_index = hash_val & 63;

    return shards_[shard_index]->Get(key, value);
}

void ShardedLruCache::Put(std::string key, std::string value) {
    size_t hash_val = std::hash<std::string>{}(key);
    size_t shard_index = hash_val & 63;

    shards_[shard_index]->Put(std::move(key), std::move(value));
}

bool ShardedLruCache::Delete(const std::string& key) {
    size_t hash_val = std::hash<std::string>{}(key);
    size_t shard_index = hash_val & 63;

    return shards_[shard_index]->Delete(key);
}
