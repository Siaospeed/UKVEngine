#ifndef UKVENGINE_SHARDED_LRU_CACHE_H_
#define UKVENGINE_SHARDED_LRU_CACHE_H_

#include <memory>
#include <string>
#include <vector>

#include "lru_cache.h"

class ShardedLruCache {
public:
    explicit ShardedLruCache(size_t capacity, size_t num_shards = 16);
    bool Get(const std::string& key, std::string* value);
    void Put(std::string key, std::string value);
    bool Delete(const std::string& key);

private:
    std::vector<std::unique_ptr<LruCache> > shards_;
};

#endif // !UKVENGINE_SHARDED_LRU_CACHE_H_
