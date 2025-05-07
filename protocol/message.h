#pragma once

class MessageWriter;

class Message {
    public:
        virtual ~Message() = default;
        virtual void Serialize(MessageWriter& w) const = 0;
};
