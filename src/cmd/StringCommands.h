#pragma once

#include "store/Database.h"

#include <string>
#include <vector>

class Connection;
class CommandTable;

/// Free functions implementing string commands: PING, SET, GET.
namespace StringCommands {

/// Register all string commands with the CommandTable.
/// Called during CommandTable construction.
void registerAll(CommandTable& table);

/// PING [message] — returns PONG or echoes the message.
void cmdPing(Database& db, Connection& conn,
             const std::vector<std::string>& args);

/// SET key value — set a key to a string value. Returns +OK.
void cmdSet(Database& db, Connection& conn,
            const std::vector<std::string>& args);

/// GET key — get the value of a key. Returns bulk string or null.
void cmdGet(Database& db, Connection& conn,
            const std::vector<std::string>& args);

}  // namespace StringCommands
