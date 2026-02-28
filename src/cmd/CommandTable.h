#pragma once

#include "store/Database.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Connection;

/// Describes one registered command.
struct CommandEntry {
    std::string name;
    int arity;       // positive = exact arg count, negative = minimum (e.g., -2 means >= 2)
    bool isWrite;    // true for SET, DEL, etc. — used by AOF in Phase 4.
    std::function<void(Database& db, Connection& conn,
                       const std::vector<std::string>& args)> handler;
};

/// Maps command names to handler functions, validates arity, dispatches.
/// Uses a simple hash map — O(1) lookup per command.
///
/// Must NOT know about: Sockets, epoll, RESP parsing internals.
class CommandTable {
public:
    /// Constructor registers all Phase 2 commands.
    CommandTable();

    /// Look up command, validate arity, call handler.
    /// Writes error responses for unknown commands or wrong arity.
    void dispatch(Database& db, Connection& conn,
                  const std::vector<std::string>& args);

    /// Register a command entry. Used by command modules during init.
    void registerCommand(CommandEntry entry);

private:
    std::unordered_map<std::string, CommandEntry> table_;
};
