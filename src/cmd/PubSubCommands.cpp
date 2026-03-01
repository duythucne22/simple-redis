#include "cmd/PubSubCommands.h"
#include "cmd/CommandTable.h"

void PubSubCommands::registerAll(CommandTable& /*table*/) {
    // SUBSCRIBE, UNSUBSCRIBE, and PUBLISH are registered in main.cpp
    // because they all need a PubSubRegistry& reference, which is
    // captured by lambda closures at registration time.
    //
    // This file exists to maintain the namespace convention for
    // potential future pub/sub commands that don't need the registry.
}
