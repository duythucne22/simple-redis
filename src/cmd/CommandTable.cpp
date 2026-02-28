#include "cmd/CommandTable.h"
#include "cmd/StringCommands.h"
#include "cmd/KeyCommands.h"
#include "net/Connection.h"
#include "proto/RespSerializer.h"

#include <algorithm>
#include <cctype>

CommandTable::CommandTable() {
    // Register all Phase 2 commands.
    StringCommands::registerAll(*this);
    KeyCommands::registerAll(*this);
}

void CommandTable::registerCommand(CommandEntry entry) {
    // Store command name in uppercase for case-insensitive lookup.
    std::string upper = entry.name;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    entry.name = upper;
    table_[upper] = std::move(entry);
}

void CommandTable::dispatch(Database& db, Connection& conn,
                            const std::vector<std::string>& args) {
    if (args.empty()) return;

    // Convert command name to uppercase for case-insensitive matching.
    std::string cmdName = args[0];
    std::transform(cmdName.begin(), cmdName.end(), cmdName.begin(), ::toupper);

    auto it = table_.find(cmdName);
    if (it == table_.end()) {
        // Unknown command.
        std::string msg = "ERR unknown command '" + args[0] + "'";
        RespSerializer::writeError(conn.outgoing(), msg);
        return;
    }

    const CommandEntry& entry = it->second;

    // Validate arity.
    int argCount = static_cast<int>(args.size());
    if (entry.arity > 0) {
        // Exact arity: args.size() must equal entry.arity.
        if (argCount != entry.arity) {
            std::string msg = "ERR wrong number of arguments for '" +
                              cmdName + "' command";
            RespSerializer::writeError(conn.outgoing(), msg);
            return;
        }
    } else {
        // Minimum arity: args.size() must be >= -entry.arity.
        if (argCount < -entry.arity) {
            std::string msg = "ERR wrong number of arguments for '" +
                              cmdName + "' command";
            RespSerializer::writeError(conn.outgoing(), msg);
            return;
        }
    }

    // Dispatch to the handler.
    entry.handler(db, conn, args);
}
