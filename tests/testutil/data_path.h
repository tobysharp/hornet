#pragma once

#include <filesystem>
#include <string>

#include <gtest/gtest.h>

namespace hornet::test {

std::filesystem::path GetDataPath(const std::string& filename);

// Get the path to a blocks data file for the current test.
inline std::filesystem::path CurrentTestVectorPath() {
  const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
  std::string filename = std::string{info->test_suite_name()} + "_" + info->name() + ".bin";
  return test::GetDataPath(filename);
}

}  // namespace hornet::test
