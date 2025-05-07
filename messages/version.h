#pragma once

#include "encoding/message_writer.h"
#include "protocol/message.h"

#include <array>
#include <string>

class VersionMessage : public Message {
public:
    int32_t version = 70015;
    uint64_t services = 0;
    int64_t timestamp = 0;
    std::array<uint8_t, 26> addr_recv{};
    std::array<uint8_t, 26> addr_from{};
    uint64_t nonce = 0;
    std::string user_agent;
    int32_t start_height = 0;
    bool relay = true;

    std::string GetName() const override {
        return "version";
    }
    
    void Serialize(MessageWriter& w) const override {
        w.WriteLE4(version);
        w.WriteLE8(services);
        w.WriteLE8(timestamp);
        w.WriteBytes(addr_recv);
        w.WriteBytes(addr_from);
        w.WriteLE8(nonce);
        w.WriteVarString(user_agent);
        w.WriteLE4(start_height);
        w.WriteBool(relay);
    }
};
