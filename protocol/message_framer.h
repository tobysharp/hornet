#pragma once

#include "encoding/message_writer.h"
#include "hash.h"
#include "protocol/constants.h"
#include "protocol/message.h"

#include <cstdint>
#include <string>
#include <vector>

class MessageFramer {
    public:
        explicit MessageFramer(Magic magic = Magic::Testnet) :
            magic_(magic) {}

        void Frame(const Message& message) {
            writer_.WriteLE4(static_cast<uint32_t>(magic_));

            // Write command (12 bytes, null-padded)
            const std::string command = message.GetName();
            std::array<char, 12> cmd = {};
            std::copy_n(command.begin(), std::min(size_t{12}, command.size()), cmd.begin());
            writer_.WriteBytes(AsByteSpan<char>(cmd));
        
            // Defer payload length (4 bytes, LE)
            const auto payload_length_index = writer_.WriteLE4(0);
        
            // Defer payload hash (4 bytes)
            const auto payload_hash_index = writer_.WriteLE4(0);
        
            // Write payload itself
            const auto payload_start_index = writer_.GetPos();
            message.Serialize(writer_);
        
            // Compute payload length and write it into the buffer
            const auto payload_length_bytes = writer_.GetPos() - payload_start_index;
            writer_.SeekPos(payload_length_index);
            writer_.WriteLE4(payload_length_bytes);
            
            // Compute payload hash and write it into the buffer
            const auto hash = DoubleSha256(std::span{Buffer().data() + payload_start_index, payload_length_bytes});
            writer_.SeekPos(payload_hash_index);
            writer_.WriteBytes({hash.data(), 4});
        }

        const std::vector<uint8_t>& Buffer() const { 
            return writer_.Buffer(); 
        }

        void Clear() {
            writer_.Clear();
        }

    private:
        Magic magic_;
        MessageWriter writer_;
};

inline std::vector<uint8_t> FrameMessage(Magic magic, const Message& message) {
    MessageFramer framer(magic);
    framer.Frame(message);
    return std::move(framer.Buffer());  // explicitly moved to avoid copy
}
