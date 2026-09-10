# LMQTT 指令集封装库
#
# 纯源码分发：不产出预编译产物，由使用方用自己的工具链编译。
# 库本身不引用任何 OS/MCU 头文件，平台能力全部经 lmqtt_port_t 注入。

add_library(lmqtt STATIC
    src/lmqtt_core.c
    src/lmqtt_cfg.c
    src/lmqtt_open.c
    src/lmqtt_close.c
    src/lmqtt_conn.c
    src/lmqtt_subunsub.c
    src/lmqtt_pub.c
    src/lmqtt_urc.c
)

target_include_directories(lmqtt
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
