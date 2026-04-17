add_library(hornetlib STATIC
   consensus/rules/scripts/sigops.cpp
   data/header_timechain.cpp
   protocol/block.cpp
   protocol/message_factory.cpp
   protocol/message_handler.cpp
   protocol/script/processor.cpp
   protocol/script/runtime/engine.cpp
   protocol/script/runtime/ops/arithmetic.cpp
   protocol/script/runtime/ops/bitwise.cpp
   protocol/script/runtime/ops/sig.cpp
   protocol/script/runtime/ops/stack.cpp
   protocol/script/runtime/signing.cpp
   util/notify.cpp
)
target_compile_features(hornetlib PRIVATE cxx_std_20)
target_include_directories(hornetlib PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hornetlib PRIVATE uring)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86|x86)$")
    target_sources(hornetlib PRIVATE crypto/sha256_ni.cpp)
    target_compile_definitions(hornetlib PUBLIC HORNET_HAS_SHA_NI=1)

    # Compile SHA-NI implementation with required CPU flags.
    set_source_files_properties(crypto/sha256_ni.cpp
        PROPERTIES COMPILE_FLAGS "-msha -msse4.1"
    )
endif()