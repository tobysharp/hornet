// message.h
#pragma once

class MessageBuffer;

class Message {
 public:
  virtual void Serialize(MessageBuffer& out) const = 0;
  virtual ~Message() = default;
};
