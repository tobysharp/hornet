#pragma once

#include "hornetlib/util/big_uint.h"

namespace hornet::crypto::ecdsa {

template <int kBits>
using UIntW = util::BigUint<kBits>;

using UInt256 = UIntW<256>;

}  // namespace hornet::crypto::ecdsa
