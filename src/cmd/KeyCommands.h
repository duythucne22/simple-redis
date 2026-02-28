#pragma once

#include "store/Database.h"

#include <string>
#include <vector>

class Connection;
class CommandTable;

/// Free functions implementing key commands: DEL, EXISTS, KEYS.
namespace KeyCommands {

/// Register all key commands with the CommandTable.
void registerAll(CommandTable& table);

/// DEL key [key ...] — delete one or more keys. Returns count deleted.
void cmdDel(Database& db, Connection& conn,
            const std::vector<std::string>& args);

/// EXISTS key [key ...] — return count of keys that exist.
void cmdExists(Database& db, Connection& conn,
               const std::vector<std::string>& args);

/// KEYS pattern — return all keys matching pattern (only * supported).
void cmdKeys(Database& db, Connection& conn,
             const std::vector<std::string>& args);

}  // namespace KeyCommands
