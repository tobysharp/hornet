#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

class Message;

class MessageFactory final {
public:
    using Error = std::runtime_error;

    // Call this method at least once with each derived Message class that the
    // factory object should be able to instantiate. Called by RegisterMessages.
    template <typename TMessage>
    void Register() {
        // Add message's concrete type to the registry
        map_.emplace(TMessage{}.GetName(), 
                    []() -> std::unique_ptr<Message> { 
                        return std::make_unique<TMessage>(); 
                    });
    }

    // Instantiates a new message of the appropriate type based on the
    // command name, e.g. "version".
    std::unique_ptr<Message> Create(const std::string_view& command) const {
        std::string name{command};
        const auto find = map_.find(name);
        if (find == map_.end())
            throw Error{"No such message type (" + name + ") registered in the factory."};
        return find->second();
    }

private:
    // Constructor is hidden: instantiate using CreateMessageFactory()
    MessageFactory() = default;
    friend MessageFactory CreateMessageFactory();

    using CreateFn = std::unique_ptr<Message>(*)();
    std::unordered_map<std::string, CreateFn> map_;
};