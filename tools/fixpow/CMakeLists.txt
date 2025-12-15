# Main app
add_executable(fixpow main.cpp)
target_compile_features(fixpow PRIVATE cxx_std_23)
target_link_libraries(fixpow PRIVATE hornetlib hornetnodelib)
