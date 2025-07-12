# Header-only core library
add_library(hornetlib STATIC
   data/header_timechain.cpp
   net/bitcoind.cpp
   net/socket.cpp
   node/protocol_loop.cpp
   node/peer_negotiator.cpp
   protocol/block.cpp
   protocol/message_factory.cpp
   protocol/message_handler.cpp
)
target_compile_features(hornetlib PRIVATE cxx_std_20)
target_include_directories(hornetlib PUBLIC ${PROJECT_SOURCE_DIR}/src)