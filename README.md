# Hornet Node

## Get the Source

Clone the repository and navigate into the project directory:

```bash
git clone https://github.com/tobysharp/hornet.git
cd hornet
```

Ensure you run all subsequent build commands from this directory.

## Build Instructions

1. Install Dependencies
You must install the specific LLVM 20 ecosystem. The build configuration explicitly links against the library paths for this specific version.

```
# Update and install basic tools
sudo apt update
sudo apt install -y build-essential cmake ninja-build git wget lsb-release liburing-dev

# Install LLVM 20, Clang 20, and the C++23-capable runtime libraries
bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)" -- 20 all
sudo apt install -y libc++-20-dev libc++abi-20-dev libunwind-20-dev
```

2. Configure and Build
The project uses presets to handle the complex flags required to link against the custom libc++.

Debug Build:

```
cmake --preset clang20-debug
cmake --build --preset clang20-debug-all
```

Release Build:
```
cmake --preset clang20-release
cmake --build --preset clang20-release-all
```

3. Verification
You can verify you are using the correct standard library by checking which shared library your binary is linked against:

```
ldd build/clang20-debug/hornetnode | grep c++
# Expected output:
# libc++.so.1 => /usr/lib/llvm-20/lib/libc++.so.1 ...
```
