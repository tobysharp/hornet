#pragma once

#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace hornet::protocol {

// The Handshake class represents the Finite State Machine for the Bitcoin protocol handshake.
// It encodes the logic of the sending and receiving of version and verack messages, to enforce
// valid handshakes between peers.
class Handshake {
 public:
  using Error = std::logic_error;

  enum class Role { Inbound, Outbound };

  enum class Transition { None, SendVersion, ReceiveVersion, SendVerack, ReceiveVerack };

  // Constructs a Handshake object based on the Role value.
  explicit Handshake(Role role)
      : role_(role), machine_(role == Role::Inbound ? s_inbound_machine_ : s_outbound_machine_),
      state_({State::Start, role_ == Role::Inbound ? Transition::None : Transition::SendVersion}) {}

  // Advances (mutates) the internal state of the handshake based on the attempted transition.
  // If the transition is illegal, the function throws an Error. Otherwise, it returns the next
  // allowable (outgoing) action if there is one.
  Transition AdvanceState(Transition transition) {
    const Source from = {state_.state, transition};
    const auto it = machine_.find(from);
    if (it == machine_.end()) Fail(transition);
    state_ = it->second;
    return state_.action;
  }

  // Returns true when the handshake has been successfully completed.
  bool IsComplete() const {
    return state_.state == State::Complete;
  }

 private:
  enum class State {
    Start,
    VersionSent,
    VersionReceived,
    VersionBoth,
    VerackSent,
    VerackReceived,
    Complete
  };

  struct Source {
    State state;
    Transition edge;
    bool operator <(const Source& rhs) const {
        return std::tie(state, edge) < std::tie(rhs.state, rhs.edge);
    }
  };

  struct Sink {
    State state;
    Transition action = Transition::None;
  };

  using StateMachine = std::map<Source, Sink>;

  Handshake() = delete;

  void Fail(Transition transition) {
    std::ostringstream oss;
    oss << "Handshake violation in state " << static_cast<int>(state_.state)
        << " with transition " << static_cast<int>(transition) << ".";
    throw Error{oss.str()};
  }  

  inline static const StateMachine s_outbound_machine_ = {
      {{State::Start, Transition::SendVersion}, {State::VersionSent}},
      {{State::VersionSent, Transition::ReceiveVersion}, {State::VersionBoth, Transition::SendVerack}},
      {{State::VersionBoth, Transition::ReceiveVerack},
       {State::VerackReceived, Transition::SendVerack}},
      {{State::VersionBoth, Transition::SendVerack}, {State::VerackSent}},
      {{State::VerackReceived, Transition::SendVerack}, {State::Complete}},
      {{State::VerackSent, Transition::ReceiveVerack}, {State::Complete}}};

  inline static const StateMachine s_inbound_machine_ = {
      {{State::Start, Transition::ReceiveVersion}, {State::VersionReceived, Transition::SendVersion}},
      {{State::VersionReceived, Transition::SendVersion}, {State::VersionBoth, Transition::SendVerack}},
      {{State::VersionBoth, Transition::ReceiveVerack},
       {State::VerackReceived, Transition::SendVerack}},
      {{State::VersionBoth, Transition::SendVerack}, {State::VerackSent}},
      {{State::VerackReceived, Transition::SendVerack}, {State::Complete}},
      {{State::VerackSent, Transition::ReceiveVerack}, {State::Complete}}};

  Role role_;
  Sink state_;
  const StateMachine& machine_;
};

}  // namespace hornet::protocol
