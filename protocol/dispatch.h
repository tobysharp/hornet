// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include "encoding/reader.h"
#include "protocol/constants.h"
#include "protocol/factory.h"
#include "protocol/message.h"
#include "protocol/parser.h"

#include <memory>
#include <span>

namespace hornet::protocol {

template <typename T = Message>
inline std::unique_ptr<T> ParseMessage(const Factory &factory, Magic magic,
                                       std::span<const uint8_t> buffer) {
  const auto parsed = Parser{magic}.Parse(buffer);
  auto msg = factory.Create(parsed.header.command);
  encoding::Reader reader{parsed.payload};
  msg->Deserialize(reader);
  return Downcast<T>(std::move(msg));
}

}  // namespace hornet::protocol
