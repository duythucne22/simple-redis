#pragma once

#include "store/Database.h"

#include <string>
#include <vector>

class Connection;
class CommandTable;

/// Free functions implementing transaction commands: MULTI, EXEC, DISCARD.
namespace TransactionCommands {

/// Register all transaction commands with the CommandTable.
void registerAll(CommandTable& table);

/// MULTI — start a transaction (enter queuing mode).
void cmdMulti(Database& db, Connection& conn,
              const std::vector<std::string>& args);

/// DISCARD — discard queued commands and leave MULTI mode.
void cmdDiscard(Database& db, Connection& conn,
                const std::vector<std::string>& args);

}  // namespace TransactionCommands
