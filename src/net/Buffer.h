#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/// A contiguous byte buffer optimized for network I/O.
/// Uses a two-cursor design (readPos, writePos) to avoid O(n) erase-from-front.
/// Implements a 3-tier compaction strategy:
///   Tier 1: Reset cursors on empty (O(1))
///   Tier 2: memmove to front when back-space is insufficient (O(readable))
///   Tier 3: Grow the vector only when compaction is not enough
class Buffer {
public:
    Buffer();

    /// Returns a pointer to the start of writable space.
    /// Used with read(): read(fd, writablePtr(), writableBytes()).
    uint8_t* writablePtr();

    /// Returns the number of bytes available for writing at the back.
    size_t writableBytes() const;

    /// Advance the write cursor after a successful read() syscall.
    void advanceWrite(size_t n);

    /// Returns a pointer to the start of unconsumed (readable) data.
    const uint8_t* readablePtr() const;

    /// Returns the number of unconsumed bytes.
    size_t readableBytes() const;

    /// Consume n bytes from the front. Resets cursors when buffer becomes empty (Tier 1).
    void consume(size_t n);

    /// Append arbitrary data to the buffer (used for building outgoing responses).
    void append(const void* data, size_t len);

    /// Ensure at least `len` bytes of writable space exist.
    /// Applies Tier 2 (compact) then Tier 3 (resize) as needed.
    void ensureWritableBytes(size_t len);

private:
    // 4KB default — matches typical network MTU and the understanding doc's
    // per-buffer memory estimate for idle connections.
    static constexpr size_t kInitialCapacity = 4096;

    std::vector<uint8_t> data_;
    size_t readPos_ = 0;   // start of unread data
    size_t writePos_ = 0;  // end of unread data (next write position)
};
