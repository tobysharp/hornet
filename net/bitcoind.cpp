// bitcoind.cpp

#include "net/bitcoind.h"

#include "net/constants.h"

#include <cstdlib>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <vector>

using namespace std::string_literals;

Bitcoind::~Bitcoind() {
    Terminate();
}

Bitcoind Bitcoind::Launch(Network network) {
    Bitcoind instance;

    // Determine network parameters
    instance.magic = GetNetworkMagic(network);
    instance.port = GetNetworkPort(network);

    // Create temp datadir
    std::ostringstream path;
    path << "/tmp/hornet_bitcoind_" << getpid();
    instance.datadir = path.str();
    std::filesystem::create_directories(instance.datadir);

    // If .lock file exists, assume bitcoind is already running there
    if (std::filesystem::exists(instance.datadir + "/.lock")) {
        instance.pid = -1;  // Do not manage this process
        return instance;
    }

    // Fork and exec bitcoind
    pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error("Failed to fork bitcoind process");
    } else if (pid == 0) {
        // Child process: build argument list
        std::vector<std::string> args = {
            "bitcoind",
            "-listen=1",
            "-datadir=" + instance.datadir,
            "-debug=net"
        };

        if (network == Network::Testnet) args.push_back("-testnet");
        if (network == Network::Regtest) args.push_back("-regtest");

        // Convert to const char* argv[]
        std::vector<const char*> argv;
        for (const auto& arg : args) argv.push_back(arg.c_str());
        argv.push_back(nullptr);

        execvp("bitcoind", const_cast<char* const*>(argv.data()));
        _exit(127);  // Only reached on exec failure
    }

    // Parent process   
    instance.pid = pid;
    sleep(1);  // Give bitcoind a moment to initialize
    return instance;
}

void Bitcoind::Terminate() {
    if (pid > 0) {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        pid = -1;
    }
}
