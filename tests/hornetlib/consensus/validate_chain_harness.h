#pragma once

#include <functional>

#include "hornetlib/consensus/types.h"

#include "testutil/blockchain.h"

namespace hornet::test {

void ExpectValidationResult(std::function<test::Blockchain()> generate,
                            consensus::Result expected = consensus::Result::Ok);

}  // namespace hornet::test
