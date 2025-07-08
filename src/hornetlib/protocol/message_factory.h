// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace hornet::protocol {

class Message;

class MessageFactory final {
 public:
  // Call this method at least once with each derived Message class that the
  // factory object should be able to instantiate. Called by RegisterMessages.
  template <typename TMessage>
  void Register() {
    // Add message's concrete type to the registry
    map_.emplace(TMessage{}.GetName(),
                 []() -> std::unique_ptr<Message> { return std::make_unique<TMessage>(); });
  }

  // Instantiates a new message of the appropriate type based on the
  // command name, e.g. "version".
  [[nodiscard]] std::unique_ptr<Message> Create(const std::string_view &command) const {
    std::string name{command};
    const auto find = map_.find(name);
    if (find == map_.end()) return nullptr;
    return find->second();
  }

  static const MessageFactory& Default();

 protected:
  MessageFactory() = default;
  void RegisterCoreMessages();

 private:
  using CreateFn = std::unique_ptr<Message> (*)();
  std::unordered_map<std::string, CreateFn> map_;
};

}  // namespace hornet::protocol
