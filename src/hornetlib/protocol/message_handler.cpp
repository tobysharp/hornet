// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#include "hornetlib/protocol/message_handler.h"

#include "hornetlib/protocol/message/block.h"
#include "hornetlib/protocol/message/getdata.h"
#include "hornetlib/protocol/message/getheaders.h"
#include "hornetlib/protocol/message/headers.h"
#include "hornetlib/protocol/message/ping.h"
#include "hornetlib/protocol/message/pong.h"
#include "hornetlib/protocol/message/sendcmpct.h"
#include "hornetlib/protocol/message/verack.h"
#include "hornetlib/protocol/message/version.h"

namespace hornet::protocol {

void MessageHandler::OnMessage(const message::Block& msg) {
  OnMessage(static_cast<const Message&>(msg));
}

void MessageHandler::OnMessage(const message::GetData& msg) {
  OnMessage(static_cast<const Message&>(msg));
}

void MessageHandler::OnMessage(const message::GetHeaders& msg) {
  OnMessage(static_cast<const Message&>(msg));
}

void MessageHandler::OnMessage(const message::Headers& msg) {
  OnMessage(static_cast<const Message&>(msg));
}

void MessageHandler::OnMessage(const message::Ping& msg) {
  OnMessage(static_cast<const Message&>(msg));
}

void MessageHandler::OnMessage(const message::Pong& msg) {
  OnMessage(static_cast<const Message&>(msg));
}

void MessageHandler::OnMessage(const message::SendCompact& msg) {
  OnMessage(static_cast<const Message&>(msg));
}

void MessageHandler::OnMessage(const message::Verack& msg) {
  OnMessage(static_cast<const Message&>(msg));
}

void MessageHandler::OnMessage(const message::Version& msg) {
  OnMessage(static_cast<const Message&>(msg));
}

}  // namespace hornet::protocol
