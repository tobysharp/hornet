#pragma once

#include "messages/version.h"
#include "protocol/message_factory.h"

inline void RegisterCoreMessages(MessageFactory& factory) {
    // Register all message types here that we want to be able
    // to instantiate on parsing prior to deserialization.
    factory.Register<VersionMessage>();
    // factory.Register<...>();
}

// Returns a message factory initialized with all the message
// types that we know how to represent with concrete classes.
inline MessageFactory CreateMessageFactory() {
    MessageFactory factory;
    RegisterCoreMessages(factory);
    return factory;
}
