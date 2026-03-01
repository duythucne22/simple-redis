#include "cmd/PubSubRegistry.h"
#include "net/Connection.h"
#include "proto/RespSerializer.h"

size_t PubSubRegistry::subscribe(const std::string& channel, Connection& conn) {
    channels_[channel].insert(&conn);
    conn.subscribedChannels.insert(channel);
    return conn.subscribedChannels.size();
}

size_t PubSubRegistry::unsubscribe(const std::string& channel, Connection& conn) {
    auto it = channels_.find(channel);
    if (it != channels_.end()) {
        it->second.erase(&conn);
        if (it->second.empty()) {
            channels_.erase(it);
        }
    }
    conn.subscribedChannels.erase(channel);
    return conn.subscribedChannels.size();
}

size_t PubSubRegistry::publish(const std::string& channel,
                                const std::string& message) {
    auto it = channels_.find(channel);
    if (it == channels_.end()) return 0;

    size_t delivered = 0;
    for (Connection* sub : it->second) {
        // Write RESP push message: *3\r\n$7\r\nmessage\r\n$<chanlen>\r\n<chan>\r\n$<msglen>\r\n<msg>\r\n
        Buffer& out = sub->outgoing();
        RespSerializer::writeArrayHeader(out, 3);
        RespSerializer::writeBulkString(out, "message");
        RespSerializer::writeBulkString(out, channel);
        RespSerializer::writeBulkString(out, message);

        // Mark subscriber as wanting to write (main loop will enable EPOLLOUT).
        sub->setWantWrite(true);
        ++delivered;
    }
    return delivered;
}

void PubSubRegistry::removeConnection(Connection& conn) {
    for (const auto& channel : conn.subscribedChannels) {
        auto it = channels_.find(channel);
        if (it != channels_.end()) {
            it->second.erase(&conn);
            if (it->second.empty()) {
                channels_.erase(it);
            }
        }
    }
    conn.subscribedChannels.clear();
}
