#pragma once

#include "store/Database.h"

#include <string>
#include <vector>

class Connection;
class CommandTable;

/// Free functions implementing sorted set commands:
/// ZADD, ZSCORE, ZRANK, ZRANGE, ZCARD, ZREM.
namespace ZSetCommands {

/// Register all sorted set commands with the CommandTable.
void registerAll(CommandTable& table);

/// ZADD key score member [score member ...] — add members with scores.
void cmdZAdd(Database& db, Connection& conn,
             const std::vector<std::string>& args);

/// ZSCORE key member — return the score of a member.
void cmdZScore(Database& db, Connection& conn,
               const std::vector<std::string>& args);

/// ZRANK key member — return the rank (0-based) of a member.
void cmdZRank(Database& db, Connection& conn,
              const std::vector<std::string>& args);

/// ZRANGE key start stop [WITHSCORES] — return elements by rank range.
void cmdZRange(Database& db, Connection& conn,
               const std::vector<std::string>& args);

/// ZCARD key — return the number of members in a sorted set.
void cmdZCard(Database& db, Connection& conn,
              const std::vector<std::string>& args);

/// ZREM key member [member ...] — remove members from a sorted set.
void cmdZRem(Database& db, Connection& conn,
             const std::vector<std::string>& args);

}  // namespace ZSetCommands
