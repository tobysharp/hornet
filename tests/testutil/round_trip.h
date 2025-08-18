// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"

namespace hornet::test {

// Serialize and deserialize to capture size info
template <typename T>
inline T RoundTrip(const T& object) {
  encoding::Writer writer;
  object.Serialize(writer);
  encoding::Reader reader(writer.Buffer());
  T obj2;
  obj2.Deserialize(reader);
  return obj2;
}

}  // namespace hornet::test
