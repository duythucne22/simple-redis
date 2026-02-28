#pragma once

#include <cstdint>
#include <string>

/// The value stored for every key in the database.
/// Phase 2: string-only. Phase 5 will extend to LIST/HASH/SET/ZSET via std::variant.
struct RedisObject {
    enum class Type : uint8_t { STRING };
    enum class Encoding : uint8_t {
        RAW,      // std::string, any binary data
        INTEGER   // int64_t, for values that are valid integers
    };

    Type type = Type::STRING;
    Encoding encoding = Encoding::RAW;
    std::string strValue;   // used when encoding == RAW
    int64_t intValue = 0;   // used when encoding == INTEGER

    /// Create a STRING RedisObject. Uses INTEGER encoding if the value
    /// is a valid int64_t, otherwise RAW.
    static RedisObject createString(const std::string& val);

    /// Return the string representation regardless of encoding.
    std::string asString() const;
};
