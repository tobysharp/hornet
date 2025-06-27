// Copyright 2025 Toby Sharp
//
// This file is part of the Hornet Node project. All rights reserved.
// For licensing or usage inquiries, contact: ask@hornetnode.com.
#include "hornetlib/protocol/handshake.h"

#include <gtest/gtest.h>

namespace hornet::protocol {
namespace {

TEST(HandshakeTest, OutboundHappyPath) {
  Handshake h(Handshake::Role::Outbound);
  EXPECT_EQ(h.AdvanceState(Handshake::Transition::Begin).next, Handshake::Transition::SendVersion);

  // Start -> SendVersion
  EXPECT_EQ(h.AdvanceState(Handshake::Transition::SendVersion).next, Handshake::Transition::None);

  // ReceiveVersion triggers SendVerack
  EXPECT_EQ(h.AdvanceState(Handshake::Transition::ReceiveVersion).next,
            Handshake::Transition::SendVerack);

  // SendVerack
  EXPECT_EQ(h.AdvanceState(Handshake::Transition::SendVerack).next, Handshake::Transition::None);

  // ReceiveVerack
  EXPECT_EQ(h.AdvanceState(Handshake::Transition::ReceiveVerack).next, Handshake::Transition::None);

  EXPECT_TRUE(h.IsComplete());
}

TEST(HandshakeTest, InboundHappyPath) {
  Handshake h(Handshake::Role::Inbound);
  EXPECT_EQ(h.AdvanceState(Handshake::Transition::Begin).next, Handshake::Transition::None);

  // ReceiveVersion triggers SendVersion
  EXPECT_EQ(h.AdvanceState(Handshake::Transition::ReceiveVersion).next,
            Handshake::Transition::SendVersion);

  // SendVersion triggers SendVerack
  EXPECT_EQ(h.AdvanceState(Handshake::Transition::SendVersion).next, Handshake::Transition::SendVerack);

  // SendVerack
  EXPECT_EQ(h.AdvanceState(Handshake::Transition::SendVerack).next, Handshake::Transition::None);

  // ReceiveVerack
  EXPECT_EQ(h.AdvanceState(Handshake::Transition::ReceiveVerack).next, Handshake::Transition::None);

  EXPECT_TRUE(h.IsComplete());
}

TEST(HandshakeTest, OutboundRejectsEarlyVerack) {
  Handshake h(Handshake::Role::Outbound);
  h.AdvanceState(Handshake::Transition::Begin);
  EXPECT_THROW(h.AdvanceState(Handshake::Transition::ReceiveVerack), Handshake::Error);
}

TEST(HandshakeTest, InboundRejectsEarlyVerack) {
  Handshake h(Handshake::Role::Inbound);
  h.AdvanceState(Handshake::Transition::Begin);
  EXPECT_THROW(h.AdvanceState(Handshake::Transition::ReceiveVerack), Handshake::Error);
}

TEST(HandshakeTest, DuplicateVersionSentFails) {
  Handshake h(Handshake::Role::Outbound);
  h.AdvanceState(Handshake::Transition::Begin);
  h.AdvanceState(Handshake::Transition::SendVersion);
  EXPECT_THROW(h.AdvanceState(Handshake::Transition::SendVersion), Handshake::Error);
}

TEST(HandshakeTest, InboundSendingVersionTooEarlyFails) {
  Handshake h(Handshake::Role::Inbound);
  h.AdvanceState(Handshake::Transition::Begin);
  EXPECT_THROW(h.AdvanceState(Handshake::Transition::SendVersion), Handshake::Error);
}

TEST(HandshakeTest, VerackBeforeVersionReceivedFails) {
  Handshake h(Handshake::Role::Outbound);
  h.AdvanceState(Handshake::Transition::Begin);
  h.AdvanceState(Handshake::Transition::SendVersion);
  EXPECT_THROW(h.AdvanceState(Handshake::Transition::SendVerack), Handshake::Error);
}

}  // namespace
}  // namespace hornet::protocol
