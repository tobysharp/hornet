add_library(hornetlib STATIC
   data/header_timechain.cpp
   protocol/block.cpp
   protocol/message_factory.cpp
   protocol/message_handler.cpp
   protocol/script/runtime/engine.cpp
   protocol/script/runtime/push.cpp
   protocol/script/processor.cpp
)
target_compile_features(hornetlib PRIVATE cxx_std_20)
target_include_directories(hornetlib PUBLIC ${PROJECT_SOURCE_DIR}/src)