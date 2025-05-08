#pragma once

#include "encoding/message_reader.h"
#include "encoding/message_writer.h"
#include "encoding/transfer.h"
#include "protocol/message.h"
#include "types.h"

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
    
    inline friend bool operator==(const VersionMessage& a, const VersionMessage& b) {
        return a.version == b.version &&
               a.services == b.services &&
               a.timestamp == b.timestamp &&
               a.addr_recv == b.addr_recv &&
               a.addr_from == b.addr_from &&
               a.nonce == b.nonce &&
               a.user_agent == b.user_agent &&
               a.start_height == b.start_height &&
               a.relay == b.relay;
    }

    void Serialize(MessageWriter& w) const override {
        Transfer(*this, w);
    }

    void Deserialize(MessageReader& r) override {
        Transfer(*this, r);
    }

 private:
    template <typename Message, typename Streamer>
    static void Transfer(Message& m, Streamer& s) {
        TransferLE4(s, m.version);
        TransferLE8(s, m.services);
        TransferLE8(s, m.timestamp);
        TransferBytes(s, AsSpan(m.addr_recv));
        TransferBytes(s, AsSpan(m.addr_from));
        TransferLE8(s, m.nonce);
        TransferVarString(s, m.user_agent);
        TransferLE4(s, m.start_height);
        TransferBool(s, m.relay);
    }
};
