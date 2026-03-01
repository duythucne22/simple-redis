#pragma once

#include "store/Database.h"

#include <string>
#include <vector>

class Connection;
class CommandTable;

/// Free functions implementing list commands:
/// LPUSH, RPUSH, LPOP, RPOP, LLEN, LRANGE.
namespace ListCommands {

/// Register all list commands with the CommandTable.
void registerAll(CommandTable& table);

/// LPUSH key element [element ...] — push elements to the head of a list.
void cmdLPush(Database& db, Connection& conn,
              const std::vector<std::string>& args);

/// RPUSH key element [element ...] — push elements to the tail of a list.
void cmdRPush(Database& db, Connection& conn,
              const std::vector<std::string>& args);

/// LPOP key — remove and return the first element of a list.
void cmdLPop(Database& db, Connection& conn,
             const std::vector<std::string>& args);

/// RPOP key — remove and return the last element of a list.
void cmdRPop(Database& db, Connection& conn,
             const std::vector<std::string>& args);

/// LLEN key — return the length of a list.
void cmdLLen(Database& db, Connection& conn,
             const std::vector<std::string>& args);

/// LRANGE key start stop — return a range of elements from a list.
void cmdLRange(Database& db, Connection& conn,
               const std::vector<std::string>& args);

}  // namespace ListCommands
