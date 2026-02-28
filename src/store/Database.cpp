#include "store/Database.h"

#include <chrono>

/// Return current time in milliseconds since epoch.
static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               system_clock::now().time_since_epoch())
        .count();
}

bool Database::checkAndExpire(const std::string& key, HTEntry* entry) {
    if (entry->expireAt < 0) return false;  // no expiry set
    if (nowMs() < entry->expireAt) return false;  // not yet expired
    table_.del(key);
    return true;
}

std::optional<std::string> Database::get(const std::string& key) {
    table_.rehashStep();

    HTEntry* entry = table_.find(key);
    if (!entry) return std::nullopt;

    // Lazy expiry: check if the key has expired.
    if (checkAndExpire(key, entry)) return std::nullopt;

    return entry->value.asString();
}

void Database::set(const std::string& key, const std::string& value) {
    table_.set(key, RedisObject::createString(value));
}

bool Database::del(const std::string& key) {
    return table_.del(key);
}

bool Database::exists(const std::string& key) {
    table_.rehashStep();

    HTEntry* entry = table_.find(key);
    if (!entry) return false;

    // Lazy expiry check.
    if (checkAndExpire(key, entry)) return false;

    return true;
}

std::vector<std::string> Database::keys() {
    table_.rehashStep();
    return table_.keys();
}

size_t Database::dbsize() const {
    return table_.size();
}

void Database::rehashStep() {
    table_.rehashStep();
}
