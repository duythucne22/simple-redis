#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "store/Skiplist.h"

/// Data type tag — matches the five Redis object types.
enum class DataType : uint8_t {
    STRING,
    LIST,
    HASH,
    SET,
    ZSET
};

/// Encoding tag — describes the internal representation.
enum class Encoding : uint8_t {
    RAW,          // std::string, any binary data
    INTEGER,      // int64_t, for values that are valid integers
    LINKEDLIST,   // std::deque<std::string> (lists)
    HASHTABLE,    // unordered_map / unordered_set (hashes, sets)
    SKIPLIST      // Skiplist + unordered_map (sorted sets)
};

/// Sorted set internal data: a skiplist for ordering plus a hash map
/// for O(1) ZSCORE lookups.
struct ZSetData {
    Skiplist skiplist;
    std::unordered_map<std::string, double> dict;

    ZSetData() = default;

    // Move-only because Skiplist is move-only.
    ZSetData(ZSetData&&) = default;
    ZSetData& operator=(ZSetData&&) = default;
    ZSetData(const ZSetData&) = delete;
    ZSetData& operator=(const ZSetData&) = delete;
};

/// The data payload. One of these alternatives is active at any time.
using RedisData = std::variant<
    std::string,                                  // STRING / RAW
    int64_t,                                      // STRING / INTEGER
    std::deque<std::string>,                      // LIST
    std::unordered_map<std::string, std::string>,  // HASH
    std::unordered_set<std::string>,              // SET
    ZSetData                                      // ZSET
>;

/// The value stored for every key in the database.
/// Supports STRING (Phase 2), LIST, HASH, SET, ZSET (Phase 5).
struct RedisObject {
    DataType type = DataType::STRING;
    Encoding encoding = Encoding::RAW;
    RedisData data;   // active alternative determined by type + encoding

    // Move-only because ZSetData (hence Skiplist) is non-copyable.
    RedisObject() = default;
    RedisObject(RedisObject&&) = default;
    RedisObject& operator=(RedisObject&&) = default;
    RedisObject(const RedisObject&) = delete;
    RedisObject& operator=(const RedisObject&) = delete;

    /// Create a STRING RedisObject. Uses INTEGER encoding if the value
    /// is a valid int64_t, otherwise RAW.
    static RedisObject createString(const std::string& val);

    /// Create an empty LIST RedisObject (std::deque).
    static RedisObject createList();

    /// Create an empty HASH RedisObject (unordered_map<string,string>).
    static RedisObject createHash();

    /// Create an empty SET RedisObject (unordered_set<string>).
    static RedisObject createSet();

    /// Create an empty ZSET RedisObject (Skiplist + dict).
    static RedisObject createZSet();

    /// Return the string representation (STRING type only).
    std::string asString() const;
};
