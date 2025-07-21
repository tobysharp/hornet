// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <mutex>

namespace hornet::data {

template <typename T>
class ReadLock {
 public:
  ReadLock(std::recursive_mutex& mutex, const T& object)
   : lock_(mutex), object_(object) {}

  const T* operator->() const { return &object_; }
  operator const T&() const { return object_; }

 private:
  std::unique_lock<std::recursive_mutex> lock_;
  const T& object_;
};

template <typename T>
class WriteLock {
 public:
  WriteLock(std::recursive_mutex& mutex, T& object)
   : lock_(mutex), object_(object) {}

  T* operator->() { return &object_; }
  operator T&() { return object_; }
 
 private:
  std::unique_lock<std::recursive_mutex> lock_;
  T& object_;
};

}  // namespace hornet::data