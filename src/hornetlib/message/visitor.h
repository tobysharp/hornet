// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

namespace hornet::message {

class Block;
class GetData;
class GetHeaders;
class Headers;
class Ping;
class Pong;
class SendCompact;
class Verack;
class Version;

class Visitor {
 public:
  virtual ~Visitor() {}
  virtual void Visit(const Block&) {}
  virtual void Visit(const GetData&) {}
  virtual void Visit(const GetHeaders&) {}
  virtual void Visit(const Headers&) {}
  virtual void Visit(const Ping&) {}
  virtual void Visit(const Pong&) {}
  virtual void Visit(const SendCompact&) {}
  virtual void Visit(const Verack&) {}
  virtual void Visit(const Version&) {}
};

}  // namespace hornet::message
