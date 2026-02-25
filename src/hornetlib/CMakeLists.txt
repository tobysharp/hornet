add_library(hornetlib STATIC
   crypto/sha256_ni.cpp
   data/header_timechain.cpp
   protocol/block.cpp
   protocol/message_factory.cpp
   protocol/message_handler.cpp
   protocol/script/runtime/engine.cpp
   protocol/script/runtime/ops/arithmetic.cpp
   protocol/script/runtime/ops/bitwise.cpp
   protocol/script/runtime/ops/stack.cpp
   protocol/script/processor.cpp
   util/notify.cpp
)
target_compile_features(hornetlib PRIVATE cxx_std_20)
target_include_directories(hornetlib PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hornetlib PRIVATE uring)

# Compile SHA-NI implementation with required CPU flags
set_source_files_properties(crypto/sha256_ni.cpp
    PROPERTIES COMPILE_FLAGS "-msha -msse4.1"
)