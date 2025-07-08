// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/message/block.h"
#include "hornetlib/protocol/message/getdata.h"
#include "hornetlib/protocol/message/getheaders.h"
#include "hornetlib/protocol/message/headers.h"
#include "hornetlib/protocol/message/ping.h"
#include "hornetlib/protocol/message/pong.h"
#include "hornetlib/protocol/message/sendcmpct.h"
#include "hornetlib/protocol/message/verack.h"
#include "hornetlib/protocol/message/version.h"
#include "hornetlib/protocol/message_factory.h"

namespace hornet::protocol {

void MessageFactory::RegisterCoreMessages() {
  // Register all message types here that we want to be able
  // to instantiate on parsing prior to deserialization.
  Register<message::Block>();
  Register<message::GetData>();
  Register<message::GetHeaders>();
  Register<message::Headers>();
  Register<message::Ping>();
  Register<message::Pong>();
  Register<message::Verack>();
  Register<message::Version>();
  Register<message::SendCompact>();
  // TODO: feefilter, etc.
}

// Returns a singleton factory instance initialized with all core message types.
// Thread-safe and initialized on first use.
/* static */ const MessageFactory& MessageFactory::Default() {
  static MessageFactory instance = [] {
    MessageFactory factory;
    factory.RegisterCoreMessages();
    return factory;
  }();
  return instance;
}

}  // namespace hornet::protocol
