// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <assert.h>

namespace hornet {

inline constexpr void Assert([[maybe_unused]] bool expression) {
  assert(expression);
}

}  // namespace hornet
