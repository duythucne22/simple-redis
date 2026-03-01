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
