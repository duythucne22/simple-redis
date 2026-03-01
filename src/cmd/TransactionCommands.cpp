#include "cmd/TransactionCommands.h"
#include "cmd/CommandTable.h"
#include "net/Connection.h"
#include "proto/RespSerializer.h"

void TransactionCommands::registerAll(CommandTable& table) {
    table.registerCommand({"MULTI",    1, false, cmdMulti});
    table.registerCommand({"DISCARD",  1, false, cmdDiscard});
    // NOTE: EXEC is registered in main.cpp because it needs CommandTable&
    // and AOFWriter& references to re-dispatch queued commands.
}

void TransactionCommands::cmdMulti(Database& /*db*/, Connection& conn,
                                    const std::vector<std::string>& /*args*/) {
    if (conn.txn.has_value()) {
        RespSerializer::writeError(conn.outgoing(),
                                   "ERR MULTI calls can not be nested");
        return;
    }
    conn.txn = TransactionState{};
    RespSerializer::writeSimpleString(conn.outgoing(), "OK");
}

void TransactionCommands::cmdDiscard(Database& /*db*/, Connection& conn,
                                      const std::vector<std::string>& /*args*/) {
    if (!conn.txn.has_value()) {
        RespSerializer::writeError(conn.outgoing(),
                                   "ERR DISCARD without MULTI");
        return;
    }
    conn.txn.reset();
    RespSerializer::writeSimpleString(conn.outgoing(), "OK");
}
