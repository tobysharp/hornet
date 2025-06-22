# Main app
add_executable(hornetnode main.cpp)
target_compile_features(hornetnode PRIVATE cxx_std_20)
target_link_libraries(hornetnode PRIVATE hornetlib)
