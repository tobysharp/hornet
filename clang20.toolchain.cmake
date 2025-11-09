# clang20.toolchain.cmake
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use LLVM libc++ headers/libs everywhere
set(CXX_LLVM_CXXV1 "/usr/lib/llvm-20/include/c++/v1")
set(CXX_LLVM_LIB "/usr/lib/llvm-20/lib")

# Minimal compile flags
# set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")

# Explicit compile and link flags
set(CMAKE_CXX_FLAGS_INIT "-nostdinc++ -isystem ${CXX_LLVM_CXXV1}")
set(CMAKE_EXE_LINKER_FLAGS_INIT " -fuse-ld=lld -rtlib=compiler-rt -unwindlib=libunwind -L${CXX_LLVM_LIB} -Wl,-rpath,${CXX_LLVM_LIB} -lc++ -lc++abi -lunwind")