#include <stack>

#include "data/header_sync.h"

namespace hornet::data {

// Perform validation on the queued headers in the current thread. Works
// until all work is done or the given timeout (in ms) expires.
void HeaderSync::Validate(const util::Timeout& timeout /* = util::Timeout::Infinite() */) {
  // For each batch in the queue, or until timeout
  for (std::optional<Batch> batch; !timeout.IsExpired() && (batch = queue_.WaitPop(timeout));) {
    if (batch->empty()) continue;

    std::optional<HeaderContext> latest = std::nullopt;
    HeaderTimechain::Position parent = timechain_.NullPosition();
    std::unique_ptr<HeaderTimechain::ValidationView> view = timechain_.GetValidationView();
    for (const auto& header : *batch) {
      auto context = validator_.ValidateDownloadedHeader(latest, header, *view);
      if (!context) return Fail();
      latest = context;
      parent =
          timechain_.IsValid(parent) ? timechain_.Add(context, parent) : timechain_.Add(context);
    }
  }
}

}  // namespace hornet::data
