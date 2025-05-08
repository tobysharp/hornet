#pragma once

#include "encoding/message_reader.h"
#include "hash.h"
#include "protocol/constants.h"

#include <cstdint>
#include <string>
#include <span>
#include <stdexcept>

class MessageParser {
public:
    using Error = std::runtime_error;

    struct ParsedMessage {
        std::string_view command;
        std::span<const uint8_t> payload;
    };

    explicit MessageParser(Magic expected_magic = Magic::Testnet)
        : magic_(expected_magic) {}

    ParsedMessage Parse(std::span<const uint8_t> buffer) const {
        if (buffer.size() < 24)
            throw Error("Message too short: requires 24-byte header.");

        MessageReader reader(buffer);

        // 1. Read and validate magic
        const Magic magic = static_cast<Magic>(reader.ReadLE4());
        if (magic != magic_) {
            throw Error("Invalid magic bytes.");
        }

        // 2. Read and map command string (12-byte null-padded ASCII)
        const auto command_bytes = reader.ReadBytes(12);
        std::string_view command = MapCommand(command_bytes);

        // 3. Read payload length
        uint32_t length = reader.ReadLE4();
        if (length > buffer.size() - 24) {
            throw Error("Declared payload length exceeds buffer size.");
        }

        // 4. Read checksum
        auto checksum = reader.ReadBytes(4);
    
        // 5. Extract payload
        auto payload = buffer.subspan(reader.GetPos(), length);

        // 6. Validate checksum
        auto hash = DoubleSha256(payload);
        if (!std::equal(checksum.begin(), checksum.end(), hash.begin())) {
            throw Error("Checksum mismatch");
        }

        return {command, payload};
    }

    static ParsedMessage Parse(Magic expected_magic, std::span<const uint8_t> buffer) {
        return MessageParser{expected_magic}.Parse(buffer);
    }

private:
    static inline std::string_view MapCommand(std::span<const uint8_t> bytes) {
        auto end = std::find(bytes.begin(), bytes.end(), 0);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                std::distance(bytes.begin(), end));
    }

    Magic magic_;
};

inline MessageParser::ParsedMessage ParseMessage(Magic magic, const std::span<const uint8_t> buffer) {
    return MessageParser{magic}.Parse(buffer);
}