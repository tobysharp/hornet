#include "protocol/message_factory.h"

#include "messages/registry.h"
#include "messages/version.h"

#include <gtest/gtest.h>

namespace {

TEST(MessageFactoryTest, CanCreateRegisteredMessage) {
    MessageFactory factory = CreateMessageFactory();

    std::unique_ptr<Message> msg = factory.Create("version");

    ASSERT_NE(msg, nullptr);
    EXPECT_EQ(msg->GetName(), "version");
    EXPECT_NE(dynamic_cast<VersionMessage*>(msg.get()), nullptr);
}

TEST(MessageFactoryTest, ThrowsOnUnknownMessage) {
    MessageFactory factory = CreateMessageFactory();

    EXPECT_THROW(factory.Create("nonexistent"), MessageFactory::Error);
}

}  // namespace
