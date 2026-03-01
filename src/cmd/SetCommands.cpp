#include "cmd/SetCommands.h"
#include "cmd/CommandTable.h"
#include "net/Connection.h"
#include "proto/RespSerializer.h"

#include <unordered_set>

static const char* WRONGTYPE =
    "WRONGTYPE Operation against a key holding the wrong kind of value";

void SetCommands::registerAll(CommandTable& table) {
    table.registerCommand({"SADD",      -3, true,  cmdSAdd});
    table.registerCommand({"SREM",      -3, true,  cmdSRem});
    table.registerCommand({"SISMEMBER",  3, false, cmdSIsMember});
    table.registerCommand({"SMEMBERS",   2, false, cmdSMembers});
    table.registerCommand({"SCARD",      2, false, cmdSCard});
}

void SetCommands::cmdSAdd(Database& db, Connection& conn,
                          const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (entry && entry->value.type != DataType::SET) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    if (!entry) {
        db.setObject(args[1], RedisObject::createSet());
        entry = db.findEntry(args[1]);
    }
    auto& set = std::get<std::unordered_set<std::string>>(entry->value.data);

    int64_t added = 0;
    for (size_t i = 2; i < args.size(); ++i) {
        if (set.insert(args[i]).second) {
            ++added;
        }
    }
    RespSerializer::writeInteger(conn.outgoing(), added);
}

void SetCommands::cmdSRem(Database& db, Connection& conn,
                          const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeInteger(conn.outgoing(), 0);
        return;
    }
    if (entry->value.type != DataType::SET) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& set = std::get<std::unordered_set<std::string>>(entry->value.data);

    int64_t removed = 0;
    for (size_t i = 2; i < args.size(); ++i) {
        removed += set.erase(args[i]);
    }
    // Auto-delete empty container.
    if (set.empty()) {
        db.del(args[1]);
    }
    RespSerializer::writeInteger(conn.outgoing(), removed);
}

void SetCommands::cmdSIsMember(Database& db, Connection& conn,
                               const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeInteger(conn.outgoing(), 0);
        return;
    }
    if (entry->value.type != DataType::SET) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& set = std::get<std::unordered_set<std::string>>(entry->value.data);
    RespSerializer::writeInteger(conn.outgoing(),
                                 set.count(args[2]) ? 1 : 0);
}

void SetCommands::cmdSMembers(Database& db, Connection& conn,
                              const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeArrayHeader(conn.outgoing(), 0);
        return;
    }
    if (entry->value.type != DataType::SET) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& set = std::get<std::unordered_set<std::string>>(entry->value.data);

    RespSerializer::writeArrayHeader(conn.outgoing(),
                                     static_cast<int64_t>(set.size()));
    for (const auto& member : set) {
        RespSerializer::writeBulkString(conn.outgoing(), member);
    }
}

void SetCommands::cmdSCard(Database& db, Connection& conn,
                           const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeInteger(conn.outgoing(), 0);
        return;
    }
    if (entry->value.type != DataType::SET) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& set = std::get<std::unordered_set<std::string>>(entry->value.data);
    RespSerializer::writeInteger(conn.outgoing(),
                                 static_cast<int64_t>(set.size()));
}
