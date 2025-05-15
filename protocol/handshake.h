#pragma once

#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace hornet::protocol {

// The Handshake class represents a FSM (Finite State Machine) for the Bitcoin protocol handshake.
// It encodes the logic of the sending and receiving of version and verack messages, to enforce
// valid handshakes between peers.
class Handshake {
 public:
  using Error = std::logic_error;  // Thrown by AdvanceState.

  // Whether the connection is outbound (we initiated) or inbound (they initiated).
  enum class Role { Inbound, Outbound };

  // All of the possible state transitions in the FSM.
  enum class Transition { None, SendVersion, ReceiveVersion, SendVerack, ReceiveVerack };

  // The actionable information returned by AdvanceState.
  struct Action {
    Transition next;      // The next transition that can be proactively executed.
    std::string command;  // The message command string for that proactive transition.
  };

  // Constructs a Handshake object based on the Role value.
  explicit Handshake(Role role) noexcept
      : role_(role),
        machine_(role == Role::Inbound ? s_inbound_machine_ : s_outbound_machine_),
        state_(
            {State::Start, role_ == Role::Inbound ? Transition::None : Transition::SendVersion}) {}

  // Advances (mutates) the internal state of the handshake based on the attempted transition.
  // If the transition is illegal, the function throws an Error. Otherwise, it returns the next
  // allowable proactive (outgoing) action if there is one.
  Action AdvanceState(Transition transition) {
    const Source from = {state_.state, transition};
    const auto it = machine_.find(from);
    if (it == machine_.end()) Fail(transition);
    state_ = it->second;
    return {state_.action, ToCommand(state_.action)};
  }

  // Returns true when the handshake has been successfully completed.
  bool IsComplete() const noexcept {
    return state_.state == State::Complete;
  }

 private:
  // All of the FSM's possible states.
  enum class State {
    Start,
    VersionSent,
    VersionReceived,
    VersionBoth,
    VerackSent,
    VerackReceived,
    Complete
  };

  // The type used for look-ups in the FSM: current state and requested transition.
  struct Source {
    State state;      // The current state
    Transition edge;  // The requested transition
    bool operator<(const Source& rhs) const {
      return std::tie(state, edge) < std::tie(rhs.state, rhs.edge);
    }
  };

  // The type used for the result of queries in the FSM: next state and proactive action.
  struct Sink {
    State state;        // The resulting state
    Transition action;  // The next proactive (send) action.
  };

  // The type for the FSM graph: a map from Source to Sink.
  using StateMachine = std::map<Source, Sink>;

  Handshake() = delete;

  static std::string ToCommand(Transition transition) {
    switch (transition) {
      case Transition::SendVersion:
        return "version";
      case Transition::SendVerack:
        return "verack";
      default:;
    }
    return {};
  }

  void Fail(Transition transition) {
    std::ostringstream oss;
    oss << "Handshake violation in state " << static_cast<int>(state_.state) << " with transition "
        << static_cast<int>(transition) << ".";
    throw Error{oss.str()};
  }

  // The state transition diagram for outbound connections
  inline static const StateMachine s_outbound_machine_ = {
      {{State::Start, Transition::SendVersion}, {State::VersionSent, Transition::None}},
      {{State::VersionSent, Transition::ReceiveVersion},       {State::VersionBoth, Transition::SendVerack}},
      {{State::VersionBoth, Transition::ReceiveVerack},       {State::VerackReceived, Transition::SendVerack}},
      {{State::VersionBoth, Transition::SendVerack}, {State::VerackSent, Transition::None}},
      {{State::VerackReceived, Transition::SendVerack}, {State::Complete, Transition::None}},
      {{State::VerackSent, Transition::ReceiveVerack}, {State::Complete, Transition::None}}};

  // The state transition diagram for inbound connections
  inline static const StateMachine s_inbound_machine_ = {
      {{State::Start, Transition::ReceiveVersion},       {State::VersionReceived, Transition::SendVersion}},
      {{State::VersionReceived, Transition::SendVersion},       {State::VersionBoth, Transition::SendVerack}},
      {{State::VersionBoth, Transition::ReceiveVerack},       {State::VerackReceived, Transition::SendVerack}},
      {{State::VersionBoth, Transition::SendVerack}, {State::VerackSent, Transition::None}},
      {{State::VerackReceived, Transition::SendVerack}, {State::Complete, Transition::None}},
      {{State::VerackSent, Transition::ReceiveVerack}, {State::Complete, Transition::None}}};

  Role role_;                    // The direction of connectivity
  Sink state_;                   // The current state and available action
  const StateMachine& machine_;  // The applicable finite state machine
};

}  // namespace hornet::protocol
