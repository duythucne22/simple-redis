#include "cmd/StringCommands.h"
#include "cmd/CommandTable.h"
#include "net/Connection.h"
#include "proto/RespSerializer.h"

void StringCommands::registerAll(CommandTable& table) {
    table.registerCommand({"PING", -1, false, cmdPing});
    table.registerCommand({"SET",   3, true,  cmdSet});
    table.registerCommand({"GET",   2, false, cmdGet});
}

void StringCommands::cmdPing(Database& /*db*/, Connection& conn,
                             const std::vector<std::string>& args) {
    if (args.size() == 1) {
        // No argument — reply with simple string PONG.
        RespSerializer::writeSimpleString(conn.outgoing(), "PONG");
    } else {
        // Echo the argument as a bulk string.
        RespSerializer::writeBulkString(conn.outgoing(), args[1]);
    }
}

void StringCommands::cmdSet(Database& db, Connection& conn,
                            const std::vector<std::string>& args) {
    // args[0] = "SET", args[1] = key, args[2] = value
    db.set(args[1], args[2]);
    RespSerializer::writeSimpleString(conn.outgoing(), "OK");
}

void StringCommands::cmdGet(Database& db, Connection& conn,
                            const std::vector<std::string>& args) {
    // args[0] = "GET", args[1] = key
    auto val = db.get(args[1]);
    if (val.has_value()) {
        RespSerializer::writeBulkString(conn.outgoing(), *val);
    } else {
        RespSerializer::writeNull(conn.outgoing());
    }
}
