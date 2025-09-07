// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/protocol/hash.h"

namespace hornet::data {

// A stable locator for looking up headers/blocks across chain re-orgs.
struct Key {
  int height = -1;
  protocol::Hash hash = {};
};

}  // namespae hornet::data
