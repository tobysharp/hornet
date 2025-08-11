// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#pragma once

#include <optional>

#include "hornetlib/data/key.h"
#include "hornetlib/data/keyframe_sidecar.h"
#include "hornetlib/data/sidecar.h"
#include "hornetlib/data/timechain.h"

namespace hornet::data {

// SidecarBinding is a template class that binds a sidecar to a timechain.
template <typename T, typename DerivedT>
class SidecarBinding {
 public:
  using Handle = Timechain::SidecarHandle<T>;

  template <typename... Args>
  static SidecarBinding Create(Timechain& timechain, Args&&... args) {
    std::unique_ptr<DerivedT> sidecar = std::make_unique<DerivedT>(std::forward<Args>(args)...);
    DerivedT& ref = *sidecar;
    Handle handle = timechain.template AddSidecar<T>(std::move(sidecar));
    return {timechain, handle, ref};
  }

  SidecarBinding(Timechain& timechain, Handle handle, DerivedT& sidecar)
    : timechain_(timechain), handle_(handle), sidecar_(sidecar) {}
  SidecarBinding(const SidecarBinding&) = default;
  SidecarBinding(SidecarBinding&&) = default;

  // Requires that the timechain's headers/sidecars mutex is locked by the current thread.
  // TODO: Assert that this precondition is met in DEBUG builds.
  DerivedT& Sidecar() { return sidecar_; }
  const DerivedT& Sidecar() const { return sidecar_; }

  [[nodiscard]] std::optional<T> Get(const Key& key) const {
    return timechain_.Get(handle_, key.height, key.hash);
  }

  void Set(const Key& key, const T& value) {
    timechain_.Set(handle_, key.height, key.hash, value);
  }

 private:
  Timechain& timechain_;
  Handle handle_;
  DerivedT& sidecar_;
};

template <typename T>
using KeyframeBinding = SidecarBinding<T, KeyframeSidecar<T>>;

}  // namespace hornet::data
