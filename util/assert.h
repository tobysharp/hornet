#pragma once

#include <assert.h>

namespace hornet {

inline void Assert([[maybe_unused]] bool expression) {
  assert(expression);
}

}  // namespace hornet
