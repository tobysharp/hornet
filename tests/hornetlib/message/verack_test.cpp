// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/message/verack.h"

#include <array>

#include "hornetlib/message/registry.h"
#include "hornetlib/message/version.h"
#include "hornetlib/net/bitcoind.h"
#include "hornetlib/net/connection.h"
#include "hornetlib/net/constants.h"
#include "hornetlib/net/receive.h"
#include "hornetlib/net/socket.h"
#include "hornetlib/protocol/constants.h"
#include "hornetlib/protocol/dispatch.h"
#include "hornetlib/protocol/factory.h"
#include "hornetlib/protocol/framer.h"
#include "hornetlib/protocol/parser.h"

#include <gtest/gtest.h>

namespace hornet::message {
namespace {

TEST(VerackMessageTest, TestVerack) {
  Verack m;
  EXPECT_EQ(m.GetName(), "verack");

  encoding::Writer writer;
  m.Serialize(writer);
  EXPECT_EQ(writer.Buffer().size(), 0);
}

}  // namespace
}  // namespace hornet::message
