#pragma once

#include "data/header_timechain.h"

namespace hornet::data {

class Timechain {
 public:
  HeaderTimechain& Headers() {
    return headers_;
  }
  const HeaderTimechain& Headers() const {
    return headers_;
  }

 private:
  HeaderTimechain headers_;
};

}  // namespace hornet::data
