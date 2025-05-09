#include "protocol/factory.h"

#include "messages/registry.h"
#include "messages/version.h"

#include <gtest/gtest.h>

namespace hornet::protocol {
namespace {

TEST(FactoryTest, CanCreateRegisteredMessage) {
  Factory factory = message::CreateMessageFactory();

  std::unique_ptr<Message> msg = factory.Create("version");

  ASSERT_NE(msg, nullptr);
  EXPECT_EQ(msg->GetName(), "version");
  EXPECT_NE(dynamic_cast<message::Version*>(msg.get()), nullptr);
}

TEST(FactoryTest, ThrowsOnUnknownMessage) {
  Factory factory = message::CreateMessageFactory();

  EXPECT_THROW(factory.Create("nonexistent"), Factory::Error);
}

}  // namespace
}  // namespace hornet::protocol
