#include "crypto/hash.h"
#include "crypto/sha256.h"
#include "types.h"

#include <array>
#include <iostream>
#include <cstring>
#include <gtest/gtest.h>

TEST(HashTest, Sha256HashOfKnownString) {
    const std::string input = "hello";
    const std::string expected = "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";
    std::ostringstream oss;
    oss << Sha256(input);
    EXPECT_EQ(oss.str(), expected);
  }

  TEST(Sha256Test, DoubleSha256HashOfKnownString) {
    const std::string input = "hello";
    const std::string expected = "9595c9df90075148eb06860365df33584b75bff782a510c6cd4883a419833d50";
    std::ostringstream oss;
    oss << DoubleSha256(input);
    EXPECT_EQ(oss.str(), expected);
  }
  