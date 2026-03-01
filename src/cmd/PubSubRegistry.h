#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Connection;

/// Central registry mapping channel names to subscriber connections.
/// Owns no connections — stores raw pointers that must be cleaned up
/// via removeConnection() before a Connection is destroyed.
///
/// Lives in the cmd/ layer. Must NOT know about: RESP, sockets, epoll.
class PubSubRegistry {
public:
    /// Subscribe a connection to a channel. Returns the total number of
    /// channels this connection is subscribed to after the operation.
    size_t subscribe(const std::string& channel, Connection& conn);

    /// Unsubscribe a connection from a channel. Returns the total number of
    /// channels this connection is subscribed to after the operation.
    size_t unsubscribe(const std::string& channel, Connection& conn);

    /// Publish a message to a channel. Returns the number of subscribers
    /// that received the message. Writes the RESP push message directly
    /// into each subscriber's outgoing buffer.
    size_t publish(const std::string& channel, const std::string& message);

    /// Remove a connection from ALL channels it is subscribed to.
    /// Must be called before a Connection is destroyed (e.g., on disconnect).
    void removeConnection(Connection& conn);

private:
    /// channel → set of subscriber Connection pointers.
    std::unordered_map<std::string, std::unordered_set<Connection*>> channels_;
};
