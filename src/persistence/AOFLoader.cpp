#include "persistence/AOFLoader.h"
#include "cmd/CommandTable.h"
#include "net/Buffer.h"
#include "net/Connection.h"
#include "proto/RespParser.h"
#include "store/Database.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int AOFLoader::load(const std::string& filename, CommandTable& cmdTable,
                    Database& db) {
    // Step 1: Open the AOF file for reading.
    int fd = ::open(filename.c_str(), O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            // INV-7: No AOF file is normal for a fresh server.
            std::printf("No AOF file found (%s), starting fresh.\n",
                        filename.c_str());
            return -1;
        }
        std::fprintf(stderr, "AOFLoader: failed to open '%s': %s\n",
                     filename.c_str(), std::strerror(errno));
        return -1;
    }

    // Step 2: Get file size and read entire file into a Buffer.
    struct stat st{};
    if (::fstat(fd, &st) < 0) {
        std::fprintf(stderr, "AOFLoader: fstat failed: %s\n",
                     std::strerror(errno));
        ::close(fd);
        return -1;
    }

    size_t fileSize = static_cast<size_t>(st.st_size);
    if (fileSize == 0) {
        std::printf("AOF file '%s' is empty, starting fresh.\n",
                    filename.c_str());
        ::close(fd);
        return 0;
    }

    Buffer buffer;
    buffer.ensureWritableBytes(fileSize);

    // Read entire file into the buffer.
    size_t totalRead = 0;
    while (totalRead < fileSize) {
        ssize_t n = ::read(fd, buffer.writablePtr(),
                           fileSize - totalRead);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "AOFLoader: read error: %s\n",
                         std::strerror(errno));
            ::close(fd);
            return -1;
        }
        if (n == 0) break;  // unexpected EOF
        buffer.advanceWrite(static_cast<size_t>(n));
        totalRead += static_cast<size_t>(n);
    }
    ::close(fd);

    // Step 3: Create a dummy Connection for dispatch (pipe-fd approach).
    // The dummy absorbs responses — we don't need them during AOF replay.
    int pipeFds[2] = {-1, -1};
    if (::pipe(pipeFds) < 0) {
        std::fprintf(stderr, "AOFLoader: pipe() failed: %s\n",
                     std::strerror(errno));
        return -1;
    }
    // Close the read end immediately — we don't need it.
    // The write end is used by the dummy Connection.
    // Responses written to the pipe are never read (pipe buffer will fill,
    // but we won't write enough during replay to block).
    ::close(pipeFds[0]);

    // Use the write end of the pipe as the dummy fd.
    // Connection takes ownership of the fd and will close it in its destructor.
    Connection dummyConn(pipeFds[1]);

    // Step 4: Parse and replay loop.
    RespParser parser;
    int count = 0;

    while (buffer.readableBytes() > 0) {
        auto cmd = parser.parse(buffer);
        if (!cmd.has_value()) {
            // INV-8: Incomplete frame = truncated AOF. Load valid prefix.
            size_t remaining = buffer.readableBytes();
            if (remaining > 0) {
                std::fprintf(stderr,
                    "AOFLoader: AOF truncated at byte %zu, "
                    "loaded %d commands (ignoring %zu trailing bytes)\n",
                    fileSize - remaining, count, remaining);
            }
            break;
        }
        if (cmd->empty()) continue;  // null array, skip

        // Replay the command through the command table.
        cmdTable.dispatch(db, dummyConn, *cmd);

        // Drain the dummy connection's outgoing buffer to prevent it from
        // growing unbounded during long replays.
        dummyConn.outgoing().consume(dummyConn.outgoing().readableBytes());

        count++;
    }

    std::printf("AOF: loaded %d commands from '%s'\n", count,
                filename.c_str());
    return count;
}
