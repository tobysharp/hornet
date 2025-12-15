#pragma once

#include <istream>
#include <ostream>
#include <span>
#include <vector>

namespace hornet::util {

template <typename T>
inline void Write(std::ostream& os, const T& data) {
  os.write(reinterpret_cast<const char*>(&data), sizeof(data));
  if (!os) ThrowRuntimeError("File write error.");
}

template <typename T>
inline void Write(std::ostream& os, const std::vector<T>& data) {
  Write(os, static_cast<uint64_t>(data.size()));
  os.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(T));
  if (!os) ThrowRuntimeError("File write error.");
}

template <typename T>
inline void Read(std::istream& is, T& data) {
  is.read(reinterpret_cast<char*>(&data), sizeof(data));
  if (!is) ThrowRuntimeError("File read error.");
}

template <typename T>
inline T Read(std::istream& is) {
  T data;
  Read(is, data);
  return data;
}

template <typename T>
inline void Read(std::istream& is, std::vector<T>& data) {
  data.resize(Read<uint64_t>(is));
  is.read(reinterpret_cast<char*>(data.data()), data.size() * sizeof(T));
  if (!is) ThrowRuntimeError("File read error.");
}

}