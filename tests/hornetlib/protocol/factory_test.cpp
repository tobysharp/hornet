// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/message_factory.h"

#include "hornetlib/protocol/message/version.h"

#include <gtest/gtest.h>

namespace hornet::protocol {
namespace {

TEST(FactoryTest, CanCreateRegisteredMessage) {
  const auto& factory = MessageFactory::Default();

  std::unique_ptr<Message> msg = factory.Create("version");

  ASSERT_NE(msg, nullptr);
  EXPECT_EQ(msg->GetName(), "version");
  EXPECT_NE(dynamic_cast<message::Version*>(msg.get()), nullptr);
}

TEST(FactoryTest, NoThrowOnUnknownMessage) {
  const auto& factory = MessageFactory::Default();

  EXPECT_EQ(factory.Create("nonexistent"), nullptr);
}

}  // namespace
}  // namespace hornet::protocol
