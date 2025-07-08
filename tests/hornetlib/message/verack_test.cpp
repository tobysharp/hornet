// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/message/verack.h"

#include <array>

#include "hornetlib/encoding/writer.h"

#include <gtest/gtest.h>

namespace hornet::protocol::message {
namespace {

TEST(VerackMessageTest, TestVerack) {
  Verack m;
  EXPECT_EQ(m.GetName(), "verack");

  encoding::Writer writer;
  m.Serialize(writer);
  EXPECT_EQ(writer.Buffer().size(), 0);
}

}  // namespace
}  // namespace hornet::protocol::message
