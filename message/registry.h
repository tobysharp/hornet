// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "message/getheaders.h"
#include "message/headers.h"
#include "message/ping.h"
#include "message/pong.h"
#include "message/sendcmpct.h"
#include "message/verack.h"
#include "message/version.h"
#include "protocol/factory.h"

namespace hornet::message {

inline void RegisterCoreMessages(protocol::Factory &factory) {
  // Register all message types here that we want to be able
  // to instantiate on parsing prior to deserialization.
  factory.Register<GetHeaders>();
  factory.Register<Headers>();
  factory.Register<Ping>();
  factory.Register<Pong>();
  factory.Register<Verack>();
  factory.Register<Version>();
  factory.Register<SendCompact>();
  // TODO: feefilter, etc.
}

// Returns a message factory initialized with all the message
// types that we know how to represent with concrete classes.
inline protocol::Factory CreateMessageFactory() {
  protocol::Factory factory;
  RegisterCoreMessages(factory);
  return factory;
}

}  // namespace hornet::message
