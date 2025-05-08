#pragma once

#include "protocol/constants.h"
#include "protocol/message.h"
#include "protocol/message_factory.h"
#include "protocol/message_parser.h"

#include <memory>
#include <span>

template <typename T = Message>
inline std::unique_ptr<T> ParseMessage(const MessageFactory& factory,
                                             Magic magic,
                                             std::span<const uint8_t> buffer) {
    const auto parsed = MessageParser{magic}.Parse(buffer);
    auto msg = factory.Create(parsed.command);
    MessageReader reader{parsed.payload};
    msg->Deserialize(reader);
    return Downcast<T>(std::move(msg));
}
