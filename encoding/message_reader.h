#pragma once

#include "encoding/endian.h"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>

class MessageReader {
public:
    explicit MessageReader(std::span<const uint8_t> buffer)
        : buffer_(buffer), pos_(0) {}

    size_t GetPos() const { return pos_; }
    
    void Seek(size_t offset) {
        if (offset > buffer_.size())
            throw std::out_of_range("Seek offset beyond buffer size");
        pos_ = offset;
    }

    bool IsEOF() const { 
        return pos_ >= buffer_.size(); 
    }

    uint8_t ReadByte() {
        RequireAvailable(1);
        return buffer_[pos_++];
    }

    uint8_t ReadBool() {
        return static_cast<bool>(ReadByte());
    }

    std::span<const uint8_t> ReadBytes(size_t len) {
        RequireAvailable(len);
        auto s = buffer_.subspan(pos_, len);
        pos_ += len;
        return s;
    }

    template <std::integral T>
    T ReadRaw() {
        RequireAvailable(sizeof(T));
        T value;
        std::memcpy(reinterpret_cast<uint8_t*>(&value), buffer_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return value;
    }

    // Read little-endian integer of type T
    template <std::integral T>
    T ReadLE() {
        return NativeToLittleEndian(ReadRaw<T>());
    }

    // Read big-endian integer of type T
    template <std::integral T>
    T ReadBE() {
        return NativeToBigEndian(ReadRaw<T>());
    }

    template <std::integral T = uint16_t>
    T ReadLE2() {
        return NarrowOrThrow<T>(ReadLE<uint16_t>());
    }

    template <std::integral T = uint32_t>
    T ReadLE4() {
        return NarrowOrThrow<T>(ReadLE<uint32_t>());

    }

    template <std::integral T = uint64_t>
    T ReadLE8() {
        return NarrowOrThrow<T>(ReadLE<uint64_t>());

    }
    
    template <std::integral T = uint16_t>
    T ReadBE2() {
        return NarrowOrThrow<T>(ReadBE<uint16_t>());

    }

    // Read a VarInt as per Bitcoin CompactSize
    template <std::unsigned_integral T = uint64_t>
    T ReadVarInt() {
        uint8_t prefix = ReadByte();
        if (prefix < 0xFD) {
            return prefix;
        } else if (prefix == 0xFD) {
            return ReadLE2<T>();
        } else if (prefix == 0xFE) {
            return ReadLE4<T>();
        } else {
            return ReadLE8<T>();
        }
    }

    std::string ReadVarString() {
        size_t len = ReadVarInt<size_t>();
        auto s = ReadBytes(len);
        return std::string(reinterpret_cast<const char*>(s.data()), s.size());
    }

private:
    void RequireAvailable(size_t len) const {
        if (pos_ + len > buffer_.size())
            throw std::out_of_range("Read exceeds buffer size");
    }

    std::span<const uint8_t> buffer_;
    size_t pos_;
};
