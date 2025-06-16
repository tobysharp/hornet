#include <stack>

#include "data/header_sync.h"

namespace hornet::data {

// Perform validation on the queued headers in the current thread. Works
// until all work is done or the given timeout (in ms) expires.
void HeaderSync::Validate(const util::Timeout& timeout /* = util::Timeout::Infinite() */) {
  // For each batch in the queue, or until timeout
  for (std::optional<Batch> batch; !timeout.IsExpired() && (batch = queue_.WaitPop(timeout));) {
    if (batch->empty()) continue;

    auto parent = timechain_.Find((*batch)[0].GetPreviousBlockHash());
    const std::unique_ptr<const HeaderTimechain::ValidationView> view = timechain_.GetValidationView(parent.iterator);
    for (const auto& header : *batch) {
      auto context = validator_.ValidateDownloadedHeader(parent.context, header, *view);
      if (!context) return Fail();
      parent.context = context;
      parent.iterator = timechain_.Add(std::move(context), parent.iterator);
    }
  }
}

}  // namespace hornet::data
