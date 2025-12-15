# Define the absolute path to the data directory
set(TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")

add_subdirectory(hornetlib)
add_subdirectory(hornetnodelib)
add_subdirectory(integration)
add_subdirectory(testutil)