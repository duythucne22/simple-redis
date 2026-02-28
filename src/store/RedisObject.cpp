#include "store/RedisObject.h"

#include <cerrno>
#include <cstdlib>
#include <climits>

RedisObject RedisObject::createString(const std::string& val) {
    RedisObject obj;
    obj.type = Type::STRING;

    // Try to store as INTEGER encoding for memory savings.
    // Only pure decimal integers (with optional leading minus) qualify.
    if (!val.empty()) {
        const char* start = val.c_str();
        char* end = nullptr;
        errno = 0;
        long long parsed = std::strtoll(start, &end, 10);
        // Must consume entire string, no overflow, and not empty.
        if (end == start + val.size() && errno == 0 &&
            parsed >= LLONG_MIN && parsed <= LLONG_MAX) {
            obj.encoding = Encoding::INTEGER;
            obj.intValue = static_cast<int64_t>(parsed);
            return obj;
        }
    }

    obj.encoding = Encoding::RAW;
    obj.strValue = val;
    return obj;
}

std::string RedisObject::asString() const {
    if (encoding == Encoding::INTEGER) {
        return std::to_string(intValue);
    }
    return strValue;
}
