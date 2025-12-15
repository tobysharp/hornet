#include <filesystem>
#include <string>
#include <fstream>
#include <stdexcept>

#include "hornetlib/util/throw.h"

namespace hornet::test {

std::filesystem::path GetDataPath(const std::string& filename) {
  // TEST_DATA_FOLDER is injected by the build system
#ifdef TEST_DATA_FOLDER
  std::filesystem::path root(TEST_DATA_FOLDER);
  return root / filename;
#else
  util::ThrowRuntimeError("TEST_DATA_FOLDER macro is not defined.");
#endif
}

} // namespace hornet::test
