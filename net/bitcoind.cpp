// bitcoind.cpp

#include "net/bitcoind.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "net/constants.h"
#include "net/socket.h"
#include "util/throw.h"

namespace hornet::net {

Bitcoind::~Bitcoind() {
  Terminate();
}

Bitcoind Bitcoind::ConnectOrLaunch(Network network) {
  try {
    return Connect(network);
  }
  catch (std::exception& e) {
    return Launch(network);
  }
}

Bitcoind Bitcoind::Connect(Network network /* = Network::Mainnet */) {
  Bitcoind instance;
  instance.network = network;
  instance.magic = GetNetworkMagic(network);
  instance.port = GetNetworkPort(network);
  instance.pid = -1;  // Not managing a process

  // Throws if not reachable
  Socket socket = Socket::Connect(kLocalhost, instance.port);

  return instance;
}

Bitcoind Bitcoind::Launch(Network network /* = Network::Regtest */) {
  Bitcoind instance;

  // Determine network parameters
  instance.network = network;
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
    std::vector<std::string> args = {"bitcoind", "-listen=1", "-datadir=" + instance.datadir,
                                     "-debug=net", "-server=1"/*, "-printtoconsole=0"*/};
                             
    if (network == Network::Testnet) args.push_back("-testnet");
    if (network == Network::Regtest) args.push_back("-regtest");

    // Convert to const char* argv[]
    std::vector<const char *> argv;
    for (const auto &arg : args) argv.push_back(arg.c_str());
    argv.push_back(nullptr);

    execvp("bitcoind", const_cast<char *const *>(argv.data()));
    _exit(127);  // Only reached on exec failure
  }

  // Parent process
  instance.pid = pid;

  // Wait for .cookie file (up to 5 seconds)
  std::string cookie = instance.GetCookiePath();
  for (int i = 0; i < 50; ++i) {
    if (std::filesystem::exists(cookie)) break;
    usleep(100000);  // 100 ms
  }
  if (!std::filesystem::exists(cookie)) {
    throw std::runtime_error("bitcoind did not create .cookie in time");
  }

  return instance;
}

std::string Bitcoind::GetCookiePath() const {
  switch (network) {
    case Network::Mainnet:
      return datadir + "/.cookie";
    case Network::Regtest:
      return datadir + "/regtest/.cookie";
    case Network::Testnet:
      return datadir + "/testnet3/.cookie";
    case Network::Signet:
      return datadir + "/signet/.cookie";
    default:
      throw std::runtime_error("Unknown network");
  }
}

std::string Bitcoind::Cli(const std::string &command) {
  std::ostringstream full_cmd;
  full_cmd << "bitcoin-cli -regtest -datadir=" << datadir << " " << command;

  FILE *pipe = popen(full_cmd.str().c_str(), "r");
  if (!pipe) throw std::runtime_error("popen() failed");

  std::string result;
  char buffer[128];
  while (fgets(buffer, sizeof(buffer), pipe)) {
    result += buffer;
  }
  pclose(pipe);
  return result;
}

void Bitcoind::MineBlocks(int n) {
  Cli("createwallet default");
  std::string address = Cli("getnewaddress");
  address.erase(address.find_last_not_of(" \n\r\t") + 1);

  std::ostringstream cmd;
  cmd << "generatetoaddress " << n << " " << address;
  Cli(cmd.str());
}

void Bitcoind::Terminate() {
  if (pid > 0) {
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
    pid = -1;
  }
  if (!datadir.empty()) {
    std::filesystem::remove_all(datadir);
  }
}

}  // namespace hornet::net
