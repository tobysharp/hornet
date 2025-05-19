#pragma once

namespace hornet::message {

class SendCompact;
class Verack;
class Version;

class Visitor {
 public:
  virtual ~Visitor() {}
  virtual void Visit(const SendCompact&) {}
  virtual void Visit(const Verack&) {}
  virtual void Visit(const Version&) {}
};

}  // namespace hornet::message
