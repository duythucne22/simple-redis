#pragma once

#include "store/Database.h"

#include <string>
#include <vector>

class Connection;
class CommandTable;
class PubSubRegistry;

/// Free functions implementing pub/sub commands: SUBSCRIBE, UNSUBSCRIBE, PUBLISH.
namespace PubSubCommands {

/// Register PUBLISH with the CommandTable. SUBSCRIBE and UNSUBSCRIBE
/// are registered separately (they need PubSubRegistry& via capture).
void registerAll(CommandTable& table);

}  // namespace PubSubCommands
