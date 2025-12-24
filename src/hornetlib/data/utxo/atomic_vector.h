#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include "hornetlib/data/utxo/single_writer.h"

namespace hornet::data::utxo {

template <typename T>
class AtomicVector {
 public:
  using Ptr = std::shared_ptr<const T>;
  using Container = std::vector<Ptr>;
  using Writer = typename SingleWriter<Container>::Writer;

  [[nodiscard]] std::shared_ptr<const Container> Snapshot() const noexcept {
    return vector_.Snapshot();
  }

  // Returns a mutable copy of the current state (lock-free).
  [[nodiscard]] std::shared_ptr<Container> Copy() const {
    return std::make_shared<Container>(*Snapshot());
  }

  [[nodiscard]] Writer Edit() noexcept {
    return vector_.Edit();
  }

  [[nodiscard]] Ptr operator[](int index) const noexcept {
    return (*Snapshot())[index];
  }

  [[nodiscard]] int Size() const noexcept {
    return std::ssize(*Snapshot());
  }

  [[nodiscard]] bool Empty() const noexcept {
    return Snapshot()->empty();
  }

  template <typename U>
  void EmplaceBack(U&& obj) {
    Edit()->emplace_back(std::make_shared<T>(std::forward<U>(obj)));
  }

  void Insert(T&& obj, auto&& compare) {
    auto edit = Edit();
    const auto it = std::lower_bound(edit->begin(), edit->end(), obj, [&](const Ptr& lhs, const T& rhs) {
      return compare(*lhs, rhs);
    });
    edit->insert(it, std::make_shared<T>(std::move(obj)));
  }

  // Insert obj if it doesn't match any existing item, otherwise overwrite the existing item.
  void InsertOrAssign(T obj, auto&& compare) {
    auto edit = Edit();
    const auto it = std::lower_bound(edit->begin(), edit->end(), obj, [&](const Ptr& lhs, const T& rhs) {
      return compare(*lhs, rhs);
    });
    if (it != edit->end()) {
      const T& existing = **it;
      if (!compare(existing, obj) && !compare(obj, existing)) {
        *it = std::make_shared<T>(std::move(obj));
        return;
      }
    }
    edit->insert(it, std::make_shared<T>(std::move(obj)));
  }

  void EraseFront(int count) {
    if (count <= 0) return;
    auto edit = Edit();
    edit->erase(edit->begin(), edit->begin() + std::min<int>(count, std::ssize(*edit)));
  }

  void EraseBack(int count) {
    if (count <= 0) return;
    auto edit = Edit();
    edit->erase(edit->end() - std::min<int>(count, std::ssize(*edit)), edit->end());
  }

  void EraseIf(auto&& predicate) {
    std::erase_if(*Edit(), [&](const Ptr& ptr) { return predicate(*ptr); });
  }

 private:
  SingleWriter<Container> vector_;
};

}  // namespace hornet::data::utxo
