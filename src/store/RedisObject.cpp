#include "store/RedisObject.h"

#include <cerrno>
#include <cstdlib>
#include <climits>

RedisObject RedisObject::createString(const std::string& val) {
    RedisObject obj;
    obj.type = DataType::STRING;

    // Try to store as INTEGER encoding for memory savings.
    if (!val.empty()) {
        const char* start = val.c_str();
        char* end = nullptr;
        errno = 0;
        long long parsed = std::strtoll(start, &end, 10);
        if (end == start + val.size() && errno == 0 &&
            parsed >= LLONG_MIN && parsed <= LLONG_MAX) {
            obj.encoding = Encoding::INTEGER;
            obj.data = static_cast<int64_t>(parsed);
            return obj;
        }
    }

    obj.encoding = Encoding::RAW;
    obj.data = val;
    return obj;
}

RedisObject RedisObject::createList() {
    RedisObject obj;
    obj.type = DataType::LIST;
    obj.encoding = Encoding::LINKEDLIST;
    obj.data = std::deque<std::string>{};
    return obj;
}

RedisObject RedisObject::createHash() {
    RedisObject obj;
    obj.type = DataType::HASH;
    obj.encoding = Encoding::HASHTABLE;
    obj.data = std::unordered_map<std::string, std::string>{};
    return obj;
}

RedisObject RedisObject::createSet() {
    RedisObject obj;
    obj.type = DataType::SET;
    obj.encoding = Encoding::HASHTABLE;
    obj.data = std::unordered_set<std::string>{};
    return obj;
}

RedisObject RedisObject::createZSet() {
    RedisObject obj;
    obj.type = DataType::ZSET;
    obj.encoding = Encoding::SKIPLIST;
    obj.data = ZSetData{};
    return obj;
}

std::string RedisObject::asString() const {
    if (encoding == Encoding::INTEGER) {
        auto* p = std::get_if<int64_t>(&data);
        return p ? std::to_string(*p) : "";
    }
    auto* p = std::get_if<std::string>(&data);
    return p ? *p : "";
}

size_t RedisObject::memoryUsage() const {
    // Base cost: the RedisObject struct itself (type, encoding, variant).
    size_t total = sizeof(RedisObject);

    // Overhead per bucket in std hash containers (estimated at pointer size).
    static constexpr size_t kBucketOverhead = sizeof(void*);

    switch (type) {
    case DataType::STRING: {
        if (encoding == Encoding::INTEGER) {
            // int64_t is stored inline in the variant — no dynamic alloc.
        } else {
            auto* p = std::get_if<std::string>(&data);
            if (p) total += p->capacity();
        }
        break;
    }
    case DataType::LIST: {
        auto* p = std::get_if<std::deque<std::string>>(&data);
        if (p) {
            for (const auto& s : *p) {
                total += sizeof(std::string) + s.capacity();
            }
        }
        break;
    }
    case DataType::HASH: {
        auto* p = std::get_if<std::unordered_map<std::string, std::string>>(&data);
        if (p) {
            // Bucket array overhead.
            total += p->bucket_count() * kBucketOverhead;
            for (const auto& [k, v] : *p) {
                total += sizeof(std::string) * 2 + k.capacity() + v.capacity();
            }
        }
        break;
    }
    case DataType::SET: {
        auto* p = std::get_if<std::unordered_set<std::string>>(&data);
        if (p) {
            total += p->bucket_count() * kBucketOverhead;
            for (const auto& m : *p) {
                total += sizeof(std::string) + m.capacity();
            }
        }
        break;
    }
    case DataType::ZSET: {
        auto* p = std::get_if<ZSetData>(&data);
        if (p) {
            // Skiplist nodes: each node has member string + score + forward ptrs.
            size_t slSize = p->skiplist.size();
            // Average level ~1.33 with p=0.25; estimate 2 pointers per node.
            static constexpr size_t kAvgPointersPerNode = 2;
            total += slSize * (sizeof(std::string) + 32 +
                               sizeof(double) +
                               kAvgPointersPerNode * sizeof(void*));
            // Dict (member → score): bucket overhead + entries.
            total += p->dict.bucket_count() * kBucketOverhead;
            for (const auto& [m, s] : p->dict) {
                total += sizeof(std::string) + m.capacity() + sizeof(double);
            }
        }
        break;
    }
    }

    return total;
}
