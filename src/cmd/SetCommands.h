#pragma once

#include "store/Database.h"

#include <string>
#include <vector>

class Connection;
class CommandTable;

/// Free functions implementing set commands:
/// SADD, SREM, SISMEMBER, SMEMBERS, SCARD.
namespace SetCommands {

/// Register all set commands with the CommandTable.
void registerAll(CommandTable& table);

/// SADD key member [member ...] — add members to a set.
void cmdSAdd(Database& db, Connection& conn,
             const std::vector<std::string>& args);

/// SREM key member [member ...] — remove members from a set.
void cmdSRem(Database& db, Connection& conn,
             const std::vector<std::string>& args);

/// SISMEMBER key member — test if member is in a set.
void cmdSIsMember(Database& db, Connection& conn,
                  const std::vector<std::string>& args);

/// SMEMBERS key — return all members of a set.
void cmdSMembers(Database& db, Connection& conn,
                 const std::vector<std::string>& args);

/// SCARD key — return the number of members in a set.
void cmdSCard(Database& db, Connection& conn,
              const std::vector<std::string>& args);

}  // namespace SetCommands
