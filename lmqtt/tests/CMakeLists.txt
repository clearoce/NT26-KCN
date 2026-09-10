add_executable(test_core test_core.c)

target_include_directories(test_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(test_core PRIVATE lmqtt)

add_test(NAME core COMMAND test_core)
