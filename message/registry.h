#pragma once

#include "message/verack.h"
#include "message/version.h"
#include "protocol/factory.h"

namespace hornet::message {

inline void RegisterCoreMessages(protocol::Factory &factory) {
  // Register all message types here that we want to be able
  // to instantiate on parsing prior to deserialization.
  factory.Register<Verack>();
  factory.Register<Version>();
  // TODO: sendcmpct, ping, feefilter, etc.
}

// Returns a message factory initialized with all the message
// types that we know how to represent with concrete classes.
inline protocol::Factory CreateMessageFactory() {
  protocol::Factory factory;
  RegisterCoreMessages(factory);
  return factory;
}

}  // namespace hornet::message
