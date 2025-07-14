// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <concepts>
#include <cstdint>
#include <string>

#include "hornetlib/encoding/reader.h"
#include "hornetlib/encoding/writer.h"

namespace hornet::encoding {

template <typename Streamer, std::integral T>
inline void TransferLE2(Streamer &s, T &field) {
  if constexpr (std::is_const_v<T>)
    s.WriteLE2(field);
  else
    s.ReadLE2(field);
}

template <typename Streamer, std::integral T>
inline void TransferBE2(Streamer &s, T &field) {
  if constexpr (std::is_const_v<T>)
    s.WriteBE2(field);
  else
    s.ReadBE2(field);
}

template <typename Streamer, std::integral T>
inline void TransferLE4(Streamer &s, T &field) {
  if constexpr (std::is_const_v<T>)
    s.WriteLE4(field);
  else
    s.ReadLE4(field);
}

template <typename Streamer, std::integral T>
inline void TransferLE8(Streamer &s, T &field) {
  if constexpr (std::is_const_v<T>)
    s.WriteLE8(field);
  else
    s.ReadLE8(field);
}

template <typename Streamer, typename Enum>
void TransferEnumLE4(Streamer& s, Enum& e) {
  using U = std::underlying_type_t<Enum>;
  if constexpr (std::is_const_v<Enum>)
    s.WriteLE4(static_cast<U>(e));
  else
    e = static_cast<Enum>(s.template ReadLE4<U>());
}

template <typename Streamer, std::integral T>
inline void TransferBytes(Streamer &s, std::span<T> field) {
  if constexpr (std::is_const_v<T>)
    s.WriteBytes(field);
  else
    s.ReadBytes(field);
}

template <typename Streamer, std::unsigned_integral T>
inline void TransferVarInt(Streamer &s, T &field) {
  if constexpr (std::is_const_v<T>)
    s.WriteVarInt(field);
  else
    s.ReadVarInt(field);
}

template <typename Streamer, typename String>
inline void TransferVarString(Streamer &s, String &field) {
  if constexpr (std::is_const_v<String>)
    s.WriteVarString(field);
  else
    s.ReadVarString(field);
}

template <typename Streamer, typename Bool>
inline void TransferBool(Streamer &s, Bool &field) {
  if constexpr (std::is_const_v<Bool>)
    s.WriteBool(field);
  else
    s.ReadBool(field);
}

template <typename Streamer, typename T>
inline void TransferObject(Streamer& s, T& field) {
  if constexpr (std::is_const_v<T>)
    field.Serialize(s);
  else
    field.Deserialize(s);
}

}  // namespace hornet::encoding
