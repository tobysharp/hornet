# Header-only core library
add_library(hornetlib STATIC
   data/header_timechain.cpp
   net/bitcoind.cpp
   net/socket.cpp
   node/processor.cpp
   node/engine.cpp
)
target_include_directories(hornetlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(hornetlib PRIVATE cxx_std_20)
