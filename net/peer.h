#pragma once

#include <string>

#include "net/connection.h"

namespace hornet::net {

enum class HandshakeState {
  None,
  OneVersion,
  TwoVersions,
  OneVerack,
  Complete,
  Failed  
};

class Peer {
  public:
  Peer(const std::string& host, uint16_t port)
      : conn_(host, port), address_(host) {}
  Peer(Connection conn, std::string address)
      : conn_(std::move(conn)), address_(std::move(address)) {}

  const std::string& Address() const { return address_; }
  Connection& GetConnection() { return conn_; }
  const Connection& GetConnection() const { return conn_; }

  HandshakeState GetHandshakeState() const { return handshake_state_; }
  void SetHandshakeState(HandshakeState state) { handshake_state_ = state; }

 private:
  Connection conn_;
  std::string address_;
  HandshakeState handshake_state_ = HandshakeState::None;

  // TODO: 
  // Magic?
  // Queues parsed messages and outgoing jobs
  // May track timestamps, misbehavior score, peer type (inbound/outbound)
  // Bridges transport <-> protocol <-> application
  // Possibly stores last send/recv time, peer ID, services, ping RTT, etc.
  // std::deque<Message> inbound
  // std::deque<SendJob> outbound
};

}  // namespace hornet::net

/*
struct SendJob {
  std::shared_ptr<const std::vector<uint8_t>> buffer;
  size_t cursor = 0;
};

struct PeerContext {
  Connection conn;
  protocol::Magic magic;
  protocol::HandshakeState handshake = protocol::HandshakeState::Disconnected;

  std::vector<uint8_t> recv_buffer;
  size_t parse_cursor = 0;

  std::deque<std::unique_ptr<protocol::Message>> inbound_queue;
  std::deque<SendJob> outbound_queue;

  protocol::MessageFactory factory = protocol::CreateMessageFactory();
};
*/