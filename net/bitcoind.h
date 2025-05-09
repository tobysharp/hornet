// bitcoind_instance.h
#pragma once

#include "protocol/constants.h"

#include <cstdint>
#include <string>
#include <sys/types.h>
#include <unistd.h>

enum class Network {
    Mainnet,
    Testnet,
    Regtest
};

struct Bitcoind {
    pid_t pid = -1;
    Magic magic;
    uint16_t port;
    std::string datadir;

    static Bitcoind Launch(Network network);
    ~Bitcoind();
    void Terminate();
};
