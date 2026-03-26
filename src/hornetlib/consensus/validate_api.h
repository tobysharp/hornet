#pragma once

#include "hornetlib/consensus/rules/validate.h"

namespace hornet::consensus {

// Export the top-level validation functions to the hornet::consensus namespace.
using rules::ValidateHeader;
using rules::ValidateTransaction;
using rules::ValidateBlock;

}  // namespace hornet::consensus
