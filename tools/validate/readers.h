// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.

#pragma once

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <type_traits>

#include "hornetlib/data/block_io.h"
#include "hornetlib/protocol/block.h"
#include "hornetlib/util/throw.h"

namespace hornet {

class ChainReader {
 public:
  ChainReader(const std::filesystem::path& folder) : dir_{folder} {
    if (!AdvanceReader())
      throw std::runtime_error{"Path given is not a valid directory containing a block file."};
  }

  explicit operator bool() const { return reader_.has_value(); }

  ChainReader& operator>>(std::shared_ptr<const protocol::Block>& block) {
    protocol::Block b;
    *reader_ >> b;
    block = std::make_shared<const protocol::Block>(std::move(b));
    if (reader_->IsEof()) AdvanceReader();
    return *this;
  }

 private:
  bool AdvanceReader() {
    std::ostringstream oss;
    oss << "blocks" << std::setfill('0') << std::setw(4) << next_ << ".bin";
    const auto path = dir_ / oss.str();
    if (!std::filesystem::exists(path)) {
      reader_.reset();
      return false;
    }
    reader_.emplace(path);
    ++next_;
    return true;
  }

  std::filesystem::path dir_;
  std::optional<data::BlockReader> reader_;
  int next_ = 0;
};

template <bool kHeadersOnly = false>
class CoreReader {
 public:
  CoreReader(const std::filesystem::path& folder) : dir_{folder} {
    ReadKey();
    if (!AdvanceReader())
      throw std::runtime_error{"Path given is not a valid directory containing a block file."};
  }

  explicit operator bool() const { return reader_.has_value(); }

  using DataType = std::conditional_t<kHeadersOnly, protocol::BlockHeader,
                                      std::shared_ptr<const protocol::Block>>;

  CoreReader& operator>>(DataType& value) {
    value = reader_->ReadNext();
    if (reader_->IsEof()) AdvanceReader();
    return *this;
  }

 private:
  void ReadKey() {
    const auto path = dir_ / "xor.dat";
    if (!std::filesystem::exists(path)) util::ThrowRuntimeError("File xor.dat is missing.");
    std::ifstream stream(path, std::ios::binary);
    stream.read(reinterpret_cast<char*>(xor_key_.data()), sizeof(xor_key_));
  }

  bool AdvanceReader() {
    std::ostringstream oss;
    oss << "blk" << std::setfill('0') << std::setw(5) << next_ << ".dat";
    const auto path = dir_ / oss.str();
    if (!std::filesystem::exists(path)) {
      reader_.reset();
      return false;
    }
    reader_.emplace(path, xor_key_);
    ++next_;
    return true;
  }

  using Reader =
      std::conditional_t<kHeadersOnly, data::SerializedHeaderReader, data::SerializedBlockReader>;

  std::filesystem::path dir_;
  std::optional<Reader> reader_;
  int next_ = 0;
  std::array<uint8_t, 8> xor_key_;
};

} // namespace hornet
