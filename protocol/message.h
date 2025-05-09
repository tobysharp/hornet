#pragma once

#include <memory>
#include <stdexcept>

namespace hornet::encoding {
// Forward declarations;
class Writer;
class Reader;
}  // namespace hornet::encoding

namespace hornet::protocol {

class Message {
 public:
  virtual ~Message() = default;
  virtual void Serialize(encoding::Writer& w) const = 0;
  virtual void Deserialize(encoding::Reader& r) = 0;
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

}  // namespace hornet::protocol
