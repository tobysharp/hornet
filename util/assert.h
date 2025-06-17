#pragma once

#include <assert.h>

namespace hornet {

void Assert([[maybe_unused]] bool expression) {
  assert(expression);
}

}  // namespace hornet
