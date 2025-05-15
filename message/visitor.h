#pragma once

namespace hornet::message {

class Version;
class Verack;

class Visitor {
 public:
  virtual ~Visitor() {}
  virtual void Visit(const Verack& verack) {}
  virtual void Visit(const Version& version) {}
};

}  // namespace hornet::message
