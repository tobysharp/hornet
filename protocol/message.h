#pragma once

#include <memory>
#include <stdexcept>

class MessageWriter;
class MessageReader;

class Message {
    public:
        virtual ~Message() = default;
        virtual void Serialize(MessageWriter& w) const = 0;
        virtual void Deserialize(MessageReader& r) = 0;
        virtual std::string GetName() const = 0;
};

template <typename T>
std::unique_ptr<T> Downcast(std::unique_ptr<Message>&& msg) {
    if (auto* ptr = dynamic_cast<T*>(msg.get())) {
        msg.release();
        return std::unique_ptr<T>{ptr};
    }
    throw std::runtime_error("Message dynamic downcast failed.");
}