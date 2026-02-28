#pragma once

#include "store/Database.h"

#include <string>
#include <vector>

class Connection;
class CommandTable;

/// Free functions implementing key commands: DEL, EXISTS, KEYS,
/// EXPIRE, TTL, PEXPIRE, PTTL, DBSIZE.
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

/// EXPIRE key seconds — set a key's TTL in seconds. Returns 1 or 0.
void cmdExpire(Database& db, Connection& conn,
               const std::vector<std::string>& args);

/// TTL key — return remaining TTL in seconds (-1 no TTL, -2 not found).
void cmdTtl(Database& db, Connection& conn,
            const std::vector<std::string>& args);

/// PEXPIRE key milliseconds — set a key's TTL in milliseconds. Returns 1 or 0.
void cmdPexpire(Database& db, Connection& conn,
                const std::vector<std::string>& args);

/// PTTL key — return remaining TTL in milliseconds (-1 no TTL, -2 not found).
void cmdPttl(Database& db, Connection& conn,
             const std::vector<std::string>& args);

/// DBSIZE — return number of keys in the database.
void cmdDbsize(Database& db, Connection& conn,
               const std::vector<std::string>& args);

}  // namespace KeyCommands
