#include "cmd/HashCommands.h"
#include "cmd/CommandTable.h"
#include "net/Connection.h"
#include "proto/RespSerializer.h"

#include <unordered_map>

static const char* WRONGTYPE =
    "WRONGTYPE Operation against a key holding the wrong kind of value";

void HashCommands::registerAll(CommandTable& table) {
    // HSET key field value [field value ...] — minimum 4 args
    table.registerCommand({"HSET",    -4, true,  cmdHSet});
    table.registerCommand({"HGET",     3, false, cmdHGet});
    table.registerCommand({"HDEL",    -3, true,  cmdHDel});
    table.registerCommand({"HGETALL",  2, false, cmdHGetAll});
    table.registerCommand({"HLEN",     2, false, cmdHLen});
}

void HashCommands::cmdHSet(Database& db, Connection& conn,
                           const std::vector<std::string>& args) {
    // args: HSET key field1 value1 [field2 value2 ...]
    // Must have even number of field-value args after key.
    if ((args.size() - 2) % 2 != 0) {
        RespSerializer::writeError(conn.outgoing(),
            "ERR wrong number of arguments for 'hset' command");
        return;
    }

    HTEntry* entry = db.findEntry(args[1]);
    if (entry && entry->value.type != DataType::HASH) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    if (!entry) {
        db.setObject(args[1], RedisObject::createHash());
        entry = db.findEntry(args[1]);
    }

    auto& hash = std::get<std::unordered_map<std::string, std::string>>(
        entry->value.data);

    int64_t added = 0;
    for (size_t i = 2; i < args.size(); i += 2) {
        auto [it, inserted] = hash.emplace(args[i], args[i + 1]);
        if (inserted) {
            ++added;
        } else {
            it->second = args[i + 1];
        }
    }
    RespSerializer::writeInteger(conn.outgoing(), added);
}

void HashCommands::cmdHGet(Database& db, Connection& conn,
                           const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeNull(conn.outgoing());
        return;
    }
    if (entry->value.type != DataType::HASH) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& hash = std::get<std::unordered_map<std::string, std::string>>(
        entry->value.data);

    auto it = hash.find(args[2]);
    if (it == hash.end()) {
        RespSerializer::writeNull(conn.outgoing());
    } else {
        RespSerializer::writeBulkString(conn.outgoing(), it->second);
    }
}

void HashCommands::cmdHDel(Database& db, Connection& conn,
                           const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeInteger(conn.outgoing(), 0);
        return;
    }
    if (entry->value.type != DataType::HASH) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& hash = std::get<std::unordered_map<std::string, std::string>>(
        entry->value.data);

    int64_t removed = 0;
    for (size_t i = 2; i < args.size(); ++i) {
        removed += hash.erase(args[i]);
    }
    // Auto-delete empty container.
    if (hash.empty()) {
        db.del(args[1]);
    }
    RespSerializer::writeInteger(conn.outgoing(), removed);
}

void HashCommands::cmdHGetAll(Database& db, Connection& conn,
                              const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeArrayHeader(conn.outgoing(), 0);
        return;
    }
    if (entry->value.type != DataType::HASH) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& hash = std::get<std::unordered_map<std::string, std::string>>(
        entry->value.data);

    // Each field-value pair = 2 elements.
    RespSerializer::writeArrayHeader(conn.outgoing(),
                                     static_cast<int64_t>(hash.size() * 2));
    for (const auto& [field, value] : hash) {
        RespSerializer::writeBulkString(conn.outgoing(), field);
        RespSerializer::writeBulkString(conn.outgoing(), value);
    }
}

void HashCommands::cmdHLen(Database& db, Connection& conn,
                           const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeInteger(conn.outgoing(), 0);
        return;
    }
    if (entry->value.type != DataType::HASH) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& hash = std::get<std::unordered_map<std::string, std::string>>(
        entry->value.data);
    RespSerializer::writeInteger(conn.outgoing(),
                                 static_cast<int64_t>(hash.size()));
}
