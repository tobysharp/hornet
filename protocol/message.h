#pragma once

#include <memory>
#include <stdexcept>

namespace hornet {

namespace encoding {
// Forward declarations;
class Writer;
class Reader;
}  // namespace encoding

namespace message {
  class Visitor;
}  // namespace message

namespace protocol {

class Message {
 public:
  virtual ~Message() = default;
  virtual void Serialize(encoding::Writer& w) const {}
  virtual void Deserialize(encoding::Reader& r) {}
  virtual std::string GetName() const = 0;
  virtual void Accept(message::Visitor& v) const = 0;
};

template <typename T>
std::unique_ptr<T> Downcast(std::unique_ptr<Message>&& msg) {
  if (auto* ptr = dynamic_cast<T*>(msg.get())) {
    msg.release();
    return std::unique_ptr<T>{ptr};
  }
  throw std::runtime_error("Message dynamic downcast failed.");
}

}  // namespace protocol
}  // namespace hornet
