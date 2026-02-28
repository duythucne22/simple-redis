#include "net/Buffer.h"

#include <cassert>
#include <cstring>  // std::memmove, std::memcpy

Buffer::Buffer()
    : data_(), readPos_(0), writePos_(0) {
    // Intentionally empty — no pre-allocation.
    // Idle connections hold zero buffer memory.
    // Memory is allocated on first ensureWritableBytes() call.
}

uint8_t* Buffer::writablePtr() {
    return data_.data() + writePos_;
}

size_t Buffer::writableBytes() const {
    return data_.size() - writePos_;
}

void Buffer::advanceWrite(size_t n) {
    // INV-2: writePos_ + n must not exceed the vector's size.
    assert(writePos_ + n <= data_.size());
    writePos_ += n;
}

const uint8_t* Buffer::readablePtr() const {
    return data_.data() + readPos_;
}

size_t Buffer::readableBytes() const {
    return writePos_ - readPos_;
}

void Buffer::consume(size_t n) {
    // INV-1: Cannot consume more bytes than are readable.
    assert(n <= readableBytes());
    readPos_ += n;

    // Tier 1: Reset cursors when all data has been consumed.
    // This is the common case for request-response patterns where each
    // message is fully processed before the next arrives. Cost: O(1).
    if (readPos_ == writePos_) {
        readPos_ = 0;
        writePos_ = 0;
    }
}

void Buffer::append(const void* data, size_t len) {
    ensureWritableBytes(len);
    std::memcpy(writablePtr(), data, len);
    advanceWrite(len);
}

void Buffer::ensureWritableBytes(size_t len) {
    if (writableBytes() >= len) {
        return;  // Enough space at the back already.
    }

    size_t readable = readableBytes();

    // Tier 2: Compact — shift readable data to front to reclaim consumed space.
    // If total capacity minus readable data is enough, memmove suffices.
    if (data_.size() >= readable + len) {
        if (readable > 0) {
            std::memmove(data_.data(), data_.data() + readPos_, readable);
        }
        readPos_ = 0;
        writePos_ = readable;
        // Now writableBytes() == data_.size() - readable >= len.
        return;
    }

    // Tier 3: Grow — compact first, then resize the vector.
    if (readable > 0) {
        std::memmove(data_.data(), data_.data() + readPos_, readable);
    }
    readPos_ = 0;
    writePos_ = readable;

    size_t needed = writePos_ + len;
    size_t newCap = data_.size();
    if (newCap == 0) {
        newCap = kInitialCapacity;
    }
    while (newCap < needed) {
        newCap *= 2;  // Double until we have enough.
    }
    data_.resize(newCap);
}
