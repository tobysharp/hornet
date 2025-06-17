#include <stack>

#include "data/header_sync.h"

namespace hornet::data {

// Perform validation on the queued headers in the current thread. Works
// until all work is done or the given timeout (in ms) expires.
void HeaderSync::Validate(const util::Timeout& timeout /* = util::Timeout::Infinite() */) {
  // For each batch in the queue, or until timeout
  for (std::optional<Batch> batch; !timeout.IsExpired() && (batch = queue_.WaitPop(timeout));) {
    if (batch->empty()) continue;

    auto [parent_iterator, parent_context] = timechain_.Find((*batch)[0].GetPreviousBlockHash());
    const std::unique_ptr<const HeaderTimechain::ValidationView> view = timechain_.GetValidationView(parent_iterator);
    for (const auto& header : *batch) {
      auto context = validator_.ValidateDownloadedHeader(parent_context, header, *view);
      if (!context) return Fail();
      parent_context = context;
      parent_iterator = timechain_.Add(std::move(*context), parent_iterator);
    }
  }
}

}  // namespace hornet::data
