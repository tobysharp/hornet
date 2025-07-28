add_library(hornetnodelib STATIC
   net/socket.cpp
   node/protocol_loop.cpp
   node/peer_negotiator.cpp
)
target_compile_features(hornetnodelib PRIVATE cxx_std_20)
target_include_directories(hornetnodelib PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hornetnodelib PRIVATE hornetlib)