#include "hornetlib/protocol/script/lang/types.h"
#include "hornetlib/protocol/script/runtime/engine.h"
#include "hornetlib/protocol/script/processor.h"
#include "hornetlib/protocol/script/satisfy.h"
#include "hornetlib/protocol/script/spend.h"

namespace hornet::protocol::script {

SatisfyResult SatisfiesLockingScript(const SpendData& spend_data, FeatureFlags features){
  using lang::Error;

  // We execute the unlocking script (scriptSig) followed by the locking script (scriptPubKey) sequentially using the
  // same stack to prevent script concatenation attacks. (See CVE-2010-5141.)

  SpendContext spend = { spend_data.tx, spend_data.input_index };
  runtime::Policy policy = { false, features.Has(Feature::StrictDER) };
  Processor processor{policy, std::make_optional(spend)};
  
  if (const auto unlock_result = processor.Run(spend_data.SignatureScript()); !unlock_result) 
    return unlock_result.Error();
  
  const auto lock_result = processor.Run(spend_data.prevout.pubkey_script);
  if (!lock_result) return lock_result.Error();
  
  return *lock_result;
}

}  // namespace hornet::protocol::script
