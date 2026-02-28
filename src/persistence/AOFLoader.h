#pragma once

#include <string>

// Forward declarations — AOFLoader only needs these interfaces.
class CommandTable;
class Database;

/// Reads the AOF file on startup, parses RESP commands using RespParser,
/// and replays them via CommandTable::dispatch() to reconstruct database state.
///
/// Sits in the persistence overlay layer. Must NOT include anything from net/
/// (except Buffer for parsing, and Connection for the dummy dispatch target).
class AOFLoader {
public:
    /// Load and replay the AOF file.
    /// Returns the number of commands replayed successfully.
    /// Returns -1 if the file was not found (normal for fresh start).
    /// On corruption/truncation, loads the valid prefix and logs a warning.
    int load(const std::string& filename, CommandTable& cmdTable,
             Database& db);
};
