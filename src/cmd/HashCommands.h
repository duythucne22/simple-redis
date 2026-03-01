#pragma once

#include "store/Database.h"

#include <string>
#include <vector>

class Connection;
class CommandTable;

/// Free functions implementing hash commands:
/// HSET, HGET, HDEL, HGETALL, HLEN.
namespace HashCommands {

/// Register all hash commands with the CommandTable.
void registerAll(CommandTable& table);

/// HSET key field value [field value ...] — set fields in a hash.
void cmdHSet(Database& db, Connection& conn,
             const std::vector<std::string>& args);

/// HGET key field — get the value of a field in a hash.
void cmdHGet(Database& db, Connection& conn,
             const std::vector<std::string>& args);

/// HDEL key field [field ...] — delete fields from a hash.
void cmdHDel(Database& db, Connection& conn,
             const std::vector<std::string>& args);

/// HGETALL key — return all field-value pairs in a hash.
void cmdHGetAll(Database& db, Connection& conn,
                const std::vector<std::string>& args);

/// HLEN key — return the number of fields in a hash.
void cmdHLen(Database& db, Connection& conn,
             const std::vector<std::string>& args);

}  // namespace HashCommands
