#pragma once

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace hornet::util {

template <typename... Args>
class Event {
 public:
  using Callback = std::function<void(Args...)>;
  using Id = int;

  Id Subscribe(Callback cb) {
    const Id id = next_id_++;    
    subscribers_.emplace_back(id, std::move(cb));
    return id;
  }

  void Unsubscribe(Id id) {
    std::erase_if(subscribers_, [id](const auto& p) { return p.first == id; });
  }

  void Broadcast(Args... args) const {
    for (const auto& pair : subscribers_)
      pair.second(args...);
  }

 private:
  Id next_id_ = 0;
  std::vector<std::pair<Id, Callback>> subscribers_;
};

}  // namespace hornet::util
