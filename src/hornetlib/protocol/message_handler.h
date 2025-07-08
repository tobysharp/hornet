// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

namespace hornet::protocol {

class Message;
namespace message {
class Block;
class GetData;
class GetHeaders;
class Headers;
class Ping;
class Pong;
class SendCompact;
class Verack;
class Version;
}  // namespace message

// Interface for protocol message dispatch (visitor pattern).
// Implemented by components that handle polymorphic Message instances.
class MessageHandler {
 public:
  virtual ~MessageHandler() = default;
  virtual void OnMessage(const Message&) {}
  virtual void OnMessage(const message::Block&);
  virtual void OnMessage(const message::GetData&);
  virtual void OnMessage(const message::GetHeaders&);
  virtual void OnMessage(const message::Headers&);
  virtual void OnMessage(const message::Ping&);
  virtual void OnMessage(const message::Pong&);
  virtual void OnMessage(const message::SendCompact&);
  virtual void OnMessage(const message::Verack&);
  virtual void OnMessage(const message::Version&);
};

}  // namespace hornet::protocol
