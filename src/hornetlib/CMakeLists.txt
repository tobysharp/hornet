# Header-only core library
add_library(hornetlib STATIC
   data/header_timechain.cpp
   net/bitcoind.cpp
   net/socket.cpp
   node/engine.cpp
   node/processor.cpp
)
target_compile_features(hornetlib PRIVATE cxx_std_20)
target_include_directories(hornetlib PUBLIC ${PROJECT_SOURCE_DIR}/src)