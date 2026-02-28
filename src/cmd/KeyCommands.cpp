#include "cmd/KeyCommands.h"
#include "cmd/CommandTable.h"
#include "net/Connection.h"
#include "proto/RespSerializer.h"

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>

/// Return current time in milliseconds since epoch.
static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               system_clock::now().time_since_epoch())
        .count();
}

/// Parse a string as int64_t. Returns false if not a valid integer.
static bool parseInteger(const std::string& s, int64_t& out) {
    if (s.empty()) return false;
    const char* start = s.c_str();
    char* end = nullptr;
    errno = 0;
    long long val = std::strtoll(start, &end, 10);
    if (end != start + s.size() || errno != 0) return false;
    out = static_cast<int64_t>(val);
    return true;
}

void KeyCommands::registerAll(CommandTable& table) {
    table.registerCommand({"DEL",     -2, true,  cmdDel});
    table.registerCommand({"EXISTS",  -2, false, cmdExists});
    table.registerCommand({"KEYS",     2, false, cmdKeys});
    table.registerCommand({"EXPIRE",   3, true,  cmdExpire});
    table.registerCommand({"TTL",      2, false, cmdTtl});
    table.registerCommand({"PEXPIRE",  3, true,  cmdPexpire});
    table.registerCommand({"PTTL",     2, false, cmdPttl});
    table.registerCommand({"DBSIZE",   1, false, cmdDbsize});
}

void KeyCommands::cmdDel(Database& db, Connection& conn,
                         const std::vector<std::string>& args) {
    // DEL key [key ...] — delete one or more keys, return count deleted.
    int64_t count = 0;
    for (size_t i = 1; i < args.size(); ++i) {
        if (db.del(args[i])) {
            ++count;
        }
    }
    RespSerializer::writeInteger(conn.outgoing(), count);
}

void KeyCommands::cmdExists(Database& db, Connection& conn,
                            const std::vector<std::string>& args) {
    // EXISTS key [key ...] — return count of keys that exist.
    int64_t count = 0;
    for (size_t i = 1; i < args.size(); ++i) {
        if (db.exists(args[i])) {
            ++count;
        }
    }
    RespSerializer::writeInteger(conn.outgoing(), count);
}

void KeyCommands::cmdKeys(Database& db, Connection& conn,
                          const std::vector<std::string>& args) {
    // KEYS pattern — only "*" is supported (return all keys).
    (void)args;  // pattern is always "*" for Phase 2.
    auto allKeys = db.keys();
    RespSerializer::writeArrayHeader(conn.outgoing(),
                                     static_cast<int64_t>(allKeys.size()));
    for (const auto& key : allKeys) {
        RespSerializer::writeBulkString(conn.outgoing(), key);
    }
}

void KeyCommands::cmdExpire(Database& db, Connection& conn,
                            const std::vector<std::string>& args) {
    // EXPIRE key seconds — set TTL. Returns 1 if key exists, 0 if not.
    int64_t seconds = 0;
    if (!parseInteger(args[2], seconds)) {
        RespSerializer::writeError(conn.outgoing(),
                                   "ERR value is not an integer or out of range");
        return;
    }

    int64_t deadlineMs = nowMs() + seconds * 1000;
    bool ok = db.setExpire(args[1], deadlineMs);
    RespSerializer::writeInteger(conn.outgoing(), ok ? 1 : 0);
}

void KeyCommands::cmdTtl(Database& db, Connection& conn,
                         const std::vector<std::string>& args) {
    // TTL key — remaining seconds, -1 (no TTL), -2 (key missing).
    int64_t remainingMs = db.ttl(args[1]);
    if (remainingMs == -1 || remainingMs == -2) {
        RespSerializer::writeInteger(conn.outgoing(), remainingMs);
    } else {
        // Convert ms to seconds, rounding down.
        RespSerializer::writeInteger(conn.outgoing(), remainingMs / 1000);
    }
}

void KeyCommands::cmdPexpire(Database& db, Connection& conn,
                             const std::vector<std::string>& args) {
    // PEXPIRE key milliseconds — set TTL in ms. Returns 1 or 0.
    int64_t ms = 0;
    if (!parseInteger(args[2], ms)) {
        RespSerializer::writeError(conn.outgoing(),
                                   "ERR value is not an integer or out of range");
        return;
    }

    int64_t deadlineMs = nowMs() + ms;
    bool ok = db.setExpire(args[1], deadlineMs);
    RespSerializer::writeInteger(conn.outgoing(), ok ? 1 : 0);
}

void KeyCommands::cmdPttl(Database& db, Connection& conn,
                          const std::vector<std::string>& args) {
    // PTTL key — remaining milliseconds, -1 (no TTL), -2 (key missing).
    int64_t remainingMs = db.ttl(args[1]);
    RespSerializer::writeInteger(conn.outgoing(), remainingMs);
}

void KeyCommands::cmdDbsize(Database& db, Connection& conn,
                            const std::vector<std::string>& args) {
    // DBSIZE — return the number of keys in the database.
    (void)args;
    RespSerializer::writeInteger(conn.outgoing(),
                                 static_cast<int64_t>(db.dbsize()));
}
