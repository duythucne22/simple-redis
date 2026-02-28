#include "cmd/KeyCommands.h"
#include "cmd/CommandTable.h"
#include "net/Connection.h"
#include "proto/RespSerializer.h"

void KeyCommands::registerAll(CommandTable& table) {
    table.registerCommand({"DEL",    -2, true,  cmdDel});
    table.registerCommand({"EXISTS", -2, false, cmdExists});
    table.registerCommand({"KEYS",    2, false, cmdKeys});
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
