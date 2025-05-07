#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

class MessageWriter {
public:
    MessageWriter() : pos_(buffer_.end()) {}

    // Write raw byte
    size_t WriteByte(uint8_t byte) {
        size_t before = GetPos();

        if (pos_ == buffer_.end()) {
            buffer_.push_back(byte);
            pos_ = buffer_.end();
        } else {
            *pos_++ = byte;
        }
        return before;
    }

    // Write raw bytes
    size_t WriteBytes(const uint8_t* data, size_t len) {
        size_t offset = GetPos();

        if (pos_ == buffer_.end()) {
            // Append to the end of the buffer
            buffer_.insert(pos_, data, data + len);
            pos_ = buffer_.end();
        } else {
            // Overwrite the current data
            auto remaining = std::distance(pos_, buffer_.end());
            size_t to_write = std::min<size_t>(remaining, len);
            std::copy_n(data, to_write, pos_);
            pos_ += to_write;
            // Appends the remaining data to the buffer end
            if (to_write < len) {
                buffer_.insert(pos_, data + to_write, data + len);
                pos_ = buffer_.end();
            }
        }
        return offset;
    }

    // Writes an integral value in little-endian order
    template <std::integral T>
    size_t WriteLE(T value) {
        if constexpr (IsLittleEndian()) {
            return WriteBytes(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
        } else {
            return WriteReverseBytes(value);
        }
    }

    // Writes an integral value in big-endian order
    template <std::integral T>
    size_t WriteBE(T value) {
        if constexpr (!IsLittleEndian()) {
            return WriteBytes(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
        } else {
            return WriteReverseBytes(value);
        }
    }

    // Writes a variable-length unsigned integer
    template <std::unsigned_integral T>
    size_t WriteVarInt(T value) {
        size_t pos = GetPos();
        if (value < 0xFD) {
            WriteByte(static_cast<uint8_t>(value));
        } else if (value <= 0xFFFF) {
            WriteByte(0xFD);
            WriteLE(static_cast<uint16_t>(value));
        } else if (value <= 0xFFFFFFFF) {
            WriteByte(0xFE);
            WriteLE(static_cast<uint32_t>(value));
        } else {
            WriteByte(0xFF);
            WriteLE(static_cast<uint64_t>(value));
        }
        return pos;
    }

    // Writes a variable-length string
    size_t WriteVarString(const std::string& s) {
        size_t pos = WriteVarInt(s.size());
        WriteBytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
        return pos;
    }

    // Returns the current seek position
    size_t GetPos() const {
        return static_cast<size_t>(std::distance(buffer_.begin(), pos_));
    }

    // Returns true when the seek position is at the end of the buffer
    bool IsEOF() const {
        return pos_ == buffer_.end();
    }

    // Sets the internal seek position to allow overwriting
    void SeekPos(size_t offset) {
        pos_ = buffer_.begin() + offset;
    }

    // Buffer access
    const std::vector<uint8_t>& Buffer() const { return buffer_; }
    std::vector<uint8_t>& Buffer() { return buffer_; }

    // Clears and resets the internal buffer and seek position
    void Clear() {
        buffer_.clear();
        pos_ = buffer_.end();
    }
    
private:
    // Returns true (at compile time) when targeting little-endian systems
    static constexpr bool IsLittleEndian() {
        union { uint16_t i; uint8_t c[2]; } test = { 0x0100 };
        return test.c[1] == 0x01;
    }

    // Write bytes in reversed order for switching endianness
    template <std::integral T>
    size_t WriteReverseBytes(T value) {
        uint8_t reversed[sizeof(T)];
        const uint8_t* begin_byte = reinterpret_cast<const uint8_t*>(&value);
        std::reverse_copy(begin_byte, begin_byte + sizeof(T), reversed);
        return WriteBytes(reversed, sizeof(T));
    }

    std::vector<uint8_t> buffer_;
    std::vector<uint8_t>::iterator pos_;
};
