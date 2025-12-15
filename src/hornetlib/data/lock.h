// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <mutex>
#include <shared_mutex>

namespace hornet::data {

template <typename T, typename Mutex>
class ReadLock {
 public:
  ReadLock(Mutex& mutex, const T& object)
   : lock_(mutex), object_(object) {}

  const T* operator->() const { return &object_; }
  operator const T&() const { return object_; }

 private:
  std::shared_lock<Mutex> lock_;
  const T& object_;
};

template <typename T, typename Mutex>
class WriteLock {
 public:
  WriteLock(Mutex& mutex, T& object)
   : lock_(mutex), object_(object) {}

  T* operator->() { return &object_; }
  operator T&() { return object_; }
 
 private:
  std::unique_lock<Mutex> lock_;
  T& object_;
};

}  // namespace hornet::data