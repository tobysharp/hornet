#include <algorithm>
#include <expected>
#include <span>
#include <unordered_map>
#include <vector>

#include "hornetlib/consensus/types.h"
#include "hornetlib/data/utxo/compact_index.h"
#include "hornetlib/data/utxo/outputs_table.h"
#include "hornetlib/data/utxo/results.h"
#include "hornetlib/data/utxo/shard.h"
#include "hornetlib/data/utxo/streaming_unspent_state.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/protocol/transaction.h"

namespace hornet::data::utxo {

consensus::Result DatabaseView::QueryPrevoutsUnspent(const protocol::Block&) const {
  // 1. Query the unspent outputs index.
  // 2. Store the query result in the cache, keyed by block hash or pointer.
  return {};
}

consensus::Result DatabaseView::EnumerateSpends(const protocol::Block&, const Callback,
                                                const void*) const {
  // 1. Retrieve the relevant query result from the cache.
  // 2. If its records haven't already been fetched, fetched them now (but ideally already happened).
  // 3. Iterate over the retrieved records in parallel, calling the callback function on each result.
  return {};
}

}  // namespace hornet::data::utxo
