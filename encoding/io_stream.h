#pragma once

#include <cstdint>
#include <iostream>

void Read(std::istream& is, char* data, size_t size) {
    is.read(data, size);
}

void Write(std::ostream& os, const char* data, size_t size) {
    os.write(data, size);
}
