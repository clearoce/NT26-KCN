cmake_minimum_required(VERSION 3.10)

project(NT26-KCN LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(NT26_BUILD_TESTS "构建 PC 端单元测试" ON)

add_subdirectory(lmqtt)

if(NT26_BUILD_TESTS)
    enable_testing()
    add_subdirectory(lmqtt/tests)
endif()
