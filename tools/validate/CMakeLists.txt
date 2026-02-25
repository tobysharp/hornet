# Main app
add_executable(validate main.cpp)
target_compile_features(validate PRIVATE cxx_std_23)
target_link_libraries(validate PRIVATE hornetlib hornetnodelib)
