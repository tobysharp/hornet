#pragma once

#include "protocol/message.h"

class VersionMessage : public Message {
    public:
        virtual void Serialize(MessageWriter& w) const override {
        }
};