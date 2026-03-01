#include "cmd/ListCommands.h"
#include "cmd/CommandTable.h"
#include "net/Connection.h"
#include "proto/RespSerializer.h"

#include <deque>

static const char* WRONGTYPE =
    "WRONGTYPE Operation against a key holding the wrong kind of value";

void ListCommands::registerAll(CommandTable& table) {
    // arity: negative means minimum arg count
    table.registerCommand({"LPUSH",  -3, true,  cmdLPush});
    table.registerCommand({"RPUSH",  -3, true,  cmdRPush});
    table.registerCommand({"LPOP",    2, true,  cmdLPop});
    table.registerCommand({"RPOP",    2, true,  cmdRPop});
    table.registerCommand({"LLEN",    2, false, cmdLLen});
    table.registerCommand({"LRANGE",  4, false, cmdLRange});
}

void ListCommands::cmdLPush(Database& db, Connection& conn,
                            const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (entry && entry->value.type != DataType::LIST) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    if (!entry) {
        db.setObject(args[1], RedisObject::createList());
        entry = db.findEntry(args[1]);
    }
    auto& list = std::get<std::deque<std::string>>(entry->value.data);
    for (size_t i = 2; i < args.size(); ++i) {
        list.push_front(args[i]);
    }
    RespSerializer::writeInteger(conn.outgoing(),
                                 static_cast<int64_t>(list.size()));
}

void ListCommands::cmdRPush(Database& db, Connection& conn,
                            const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (entry && entry->value.type != DataType::LIST) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    if (!entry) {
        db.setObject(args[1], RedisObject::createList());
        entry = db.findEntry(args[1]);
    }
    auto& list = std::get<std::deque<std::string>>(entry->value.data);
    for (size_t i = 2; i < args.size(); ++i) {
        list.push_back(args[i]);
    }
    RespSerializer::writeInteger(conn.outgoing(),
                                 static_cast<int64_t>(list.size()));
}

void ListCommands::cmdLPop(Database& db, Connection& conn,
                           const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeNull(conn.outgoing());
        return;
    }
    if (entry->value.type != DataType::LIST) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& list = std::get<std::deque<std::string>>(entry->value.data);
    if (list.empty()) {
        RespSerializer::writeNull(conn.outgoing());
        return;
    }
    std::string val = std::move(list.front());
    list.pop_front();
    // Auto-delete empty containers.
    if (list.empty()) {
        db.del(args[1]);
    }
    RespSerializer::writeBulkString(conn.outgoing(), val);
}

void ListCommands::cmdRPop(Database& db, Connection& conn,
                           const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeNull(conn.outgoing());
        return;
    }
    if (entry->value.type != DataType::LIST) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& list = std::get<std::deque<std::string>>(entry->value.data);
    if (list.empty()) {
        RespSerializer::writeNull(conn.outgoing());
        return;
    }
    std::string val = std::move(list.back());
    list.pop_back();
    if (list.empty()) {
        db.del(args[1]);
    }
    RespSerializer::writeBulkString(conn.outgoing(), val);
}

void ListCommands::cmdLLen(Database& db, Connection& conn,
                           const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeInteger(conn.outgoing(), 0);
        return;
    }
    if (entry->value.type != DataType::LIST) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& list = std::get<std::deque<std::string>>(entry->value.data);
    RespSerializer::writeInteger(conn.outgoing(),
                                 static_cast<int64_t>(list.size()));
}

void ListCommands::cmdLRange(Database& db, Connection& conn,
                             const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeArrayHeader(conn.outgoing(), 0);
        return;
    }
    if (entry->value.type != DataType::LIST) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& list = std::get<std::deque<std::string>>(entry->value.data);
    int n = static_cast<int>(list.size());

    int start = std::stoi(args[2]);
    int stop  = std::stoi(args[3]);

    // Convert negative indices.
    if (start < 0) start += n;
    if (stop < 0)  stop += n;
    // Clamp.
    if (start < 0) start = 0;
    if (stop >= n) stop = n - 1;

    if (start > stop || start >= n) {
        RespSerializer::writeArrayHeader(conn.outgoing(), 0);
        return;
    }

    int count = stop - start + 1;
    RespSerializer::writeArrayHeader(conn.outgoing(), count);
    for (int i = start; i <= stop; ++i) {
        RespSerializer::writeBulkString(conn.outgoing(), list[i]);
    }
}
