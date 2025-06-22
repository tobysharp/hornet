// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "message/verack.h"

#include <array>

#include "message/registry.h"
#include "message/version.h"
#include "net/bitcoind.h"
#include "net/connection.h"
#include "net/constants.h"
#include "net/receive.h"
#include "net/socket.h"
#include "protocol/constants.h"
#include "protocol/dispatch.h"
#include "protocol/factory.h"
#include "protocol/framer.h"
#include "protocol/parser.h"

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
