add_library(hornetnodelib STATIC
   controller.cpp
   dispatch/protocol_loop.cpp
   dispatch/peer_negotiator.cpp
   net/socket.cpp
)
target_compile_features(hornetnodelib PRIVATE cxx_std_20)
target_include_directories(hornetnodelib PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hornetnodelib PRIVATE hornetlib)