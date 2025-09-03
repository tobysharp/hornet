#include "hornetlib/util/notify.h"

#include <gtest/gtest.h>

#include "hornetlib/util/log.h"

namespace hornet::util {
namespace {

TEST(NotifyTest, TestLogAll) {
    LogDebug() << "Yawn, it's debugging.";
    LogInfo() << "Phew, it's only info.";
    LogWarn() << "Uh oh! It's a warning!";
    LogError() << "Oh no! It's an error! :(";
}

TEST(NotifyTest, TestLogInfo) {
    LogContext::Instance().SetLevel(LogLevel::Info);
    LogDebug() << "Yawn, it's debugging.";  // Should be dropped
    LogInfo() << "Phew, it's only info.";
    LogWarn() << "Uh oh! It's a warning!";
    LogError() << "Oh no! It's an error! :(";
}

}  // namespace
}  // namespace hornet::util
