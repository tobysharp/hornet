# Main app
add_executable(writechain main.cpp)
target_compile_features(writechain PRIVATE cxx_std_23)
target_link_libraries(writechain PRIVATE hornetlib hornetnodelib)
