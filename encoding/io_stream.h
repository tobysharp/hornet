#pragma once

#include "io.h"

#include <cstdint>
#include <iostream>

namespace io {

void Dispatch(ReadTag, std::istream& is, char* data, size_t size) {
    is.read(data, size);
}

void Dispatch(WriteTag, std::ostream& os, const char* data, size_t size) {
    os.write(data, size);
}

}  // namespace io
