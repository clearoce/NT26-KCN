# AT 传输层
#
# 纯源码分发：不产出预编译产物，由使用方用自己的工具链编译。
# 库不引用任何 OS/MCU 头文件，平台能力全部经 at_port_t 注入。
#
# 日志与断言是**编译期**接入的：使用方定义 AT_CONFIG_FILE=<自己的配置头>，
# 在其中给出 AT_LOG_IMPL / AT_ASSERT_IMPL；不定义则展开为空宏。

add_library(at STATIC
    src/at_client.c
)

target_include_directories(at
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
