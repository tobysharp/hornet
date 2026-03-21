#include "hornetlib/consensus/rules/validate_spending.h"

#include "hornetlib/protocol/transaction.h"

#include <gtest/gtest.h>

namespace hornet::consensus::rules {
namespace {

TEST(ValidateSpendingInputTest, EnforcesCoinbaseMaturityBoundary) {
  protocol::Transaction funding_tx;
  SpendRecord spend{
      .funding_height = 1000, .funding_flags = 1, .amount = 50'000'000, .pubkey_script = {}, .spend_input_index = 0};

  EXPECT_EQ(ValidateCoinbaseMaturity(InputSpendContext{.tx = funding_tx, .spend = spend, .height = 1099}),
            Error::Spending_PrematureSpend);
  EXPECT_TRUE(ValidateCoinbaseMaturity(InputSpendContext{.tx = funding_tx, .spend = spend, .height = 1100}));
}

}  // namespace
}  // namespace hornet::consensus::rules