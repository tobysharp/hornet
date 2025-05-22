#pragma once

namespace hornet::message {

  class GetHeaders;
  class Ping;
class Pong;
class SendCompact;
class Verack;
class Version;

class Visitor {
 public:
  virtual ~Visitor() {}
  virtual void Visit(const GetHeaders&) {}
  virtual void Visit(const Ping&) {}
  virtual void Visit(const Pong&) {}
  virtual void Visit(const SendCompact&) {}
  virtual void Visit(const Verack&) {}
  virtual void Visit(const Version&) {}
};

}  // namespace hornet::message
