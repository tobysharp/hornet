#pragma once

#include "hornetlib/util/big_uint.h"

namespace hornet::crypto::ecdsa {

template <int kBits>
using UIntW = util::BigUint<kBits>;

}  // namespace hornet::crypto::ecdsa