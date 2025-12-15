#pragma once

#include <filesystem>
#include <iomanip>
#include <sstream>

namespace hornet::test {

class TempFolder {
 public:
  TempFolder() {
    namespace fs = std::filesystem;
    const auto base = fs::temp_directory_path();

    int id = 1;
    fs::path candidate;
    do {
      std::ostringstream oss;
      oss << "testdir_" << std::setw(4) << std::setfill('0') << id++;
      candidate = base / oss.str();
    } while (fs::exists(candidate));

    fs::create_directory(path_ = candidate);
  }

  ~TempFolder() noexcept {
    try {
      std::filesystem::remove_all(path_);
    } catch (...) {
    }
  }

  const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace hornet::test