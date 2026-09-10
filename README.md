# NT26-KCN

NT26 模组的 AT 指令集封装库。按功能域划分一级目录，`lmqtt/` 是第一个。

## 为什么是纯源码

本库**只提供源码，不提供预编译产物**。

预编译库是跨平台的障碍而非助力——一种工具链 + 一种架构就要出一个产物，还要维护版本矩阵；同时它会让调用方失去调试能力（无法单步、无法看变量、出问题只能反汇编）。跨平台的关键在移植层抽象，不在产物形态。

## lmqtt

把 NT26 内建 MQTT（`AT+LMQTT*`）封装成一套与平台无关的函数库。设计依据是
《Lierda NT26 MQTT 协议指令》手册，文件划分与手册章节一一对应，便于对照。

### 设计要点

| 要点 | 说明 |
| --- | --- |
| **不依赖任何 OS/MCU** | 平台能力（串口写、互斥、信号量）全部通过 `lmqtt_port_t` 注入 |
| **不创建任务** | 纯被动：宿主在串口 ISR/接收任务里喂字节，库内部组帧分发 |
| **下行用轮询交付** | `lmqtt_take_downlink()` 由业务任务主动取走，解析在自己的栈上做 |
| **按 msgID 匹配响应** | 迟到/串扰的 URC 不会被误判为当前命令的结果 |
| **不掺业务** | 不含 MQTT 报文格式解析、JSON、认证算法——只做 AT 封装 |

### 目录结构

```
lmqtt/
├── include/                 对外头文件
│   ├── lmqtt.h              总入口
│   ├── lmqtt_types.h        类型、错误码、超时常量（取自手册）
│   ├── lmqtt_port.h         移植层接口
│   ├── lmqtt_core.h         实例与 AT 引擎
│   ├── lmqtt_cfg.h          AT+LMQTTCFG
│   ├── lmqtt_open.h         AT+LMQTTOPEN
│   ├── lmqtt_close.h        AT+LMQTTCLOSE
│   ├── lmqtt_conn.h         AT+LMQTTCONN
│   ├── lmqtt_disc.h         AT+LMQTTDISC
│   ├── lmqtt_subunsub.h     AT+LMQTTSUBUNSUB
│   ├── lmqtt_pub.h          AT+LMQTTPUB
│   ├── lmqtt_pubex.h        AT+LMQTTPUBEX
│   ├── lmqtt_read.h         AT+LMQTTREAD
│   └── lmqtt_urc.h          +LMQTTURC
├── src/
│   ├── lmqtt_core.c         AT 引擎（行组帧 / URC 分发 / 命令配对）
│   └── lmqtt_internal.h     库内部接口
└── tests/                   PC 端单元测试
```

### 构建与测试

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

不装 CMake 也可以直接编译测试：

```bash
cd lmqtt/tests
gcc -Wall -Wextra -I../include -I../src -o test_core test_core.c ../src/lmqtt_core.c
./test_core
```

### 移植

宿主需要实现 `lmqtt_port_t` 的几项能力：

| 成员 | 用途 |
| --- | --- |
| `write` | 向模组串口写字节 |
| `mutex_create/lock/unlock` | 串行化一次完整的命令事务（单任务调用可全部留 NULL） |
| `sem_create/take/give/reset` | 命令完成通知（二值信号量） |

日志可选，通过编译期宏接入，默认编译为空：

```c
#define LMQTT_LOG_IMPL(lv, fmt, ...)  my_log(lv, "[LMQTT] " fmt, ##__VA_ARGS__)
```

### 用法

```c
static lmqtt_t ctx;

lmqtt_init(&ctx, &my_port);
lmqtt_set_stats_cb(&ctx, on_stats, NULL);

/* 串口中断或接收任务 */
void uart_rx_isr(const uint8_t *buf, size_t len)
{
    lmqtt_rx_feed(&ctx, buf, len);
}

/* 业务任务 */
lmqtt_pub(&ctx, msgid, LMQTT_QOS1, false, topic, payload, len, LMQTT_TMO_PUB);

const char *dn = lmqtt_take_downlink(&ctx);
if (dn != NULL) {
    /* 在本任务栈上解析 —— 不要塞进接收上下文 */
}
```

## 实施状态

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| 1 | 骨架、类型/移植层、AT 引擎 + PC 单元测试 | 完成（25 项断言通过） |
| 2 | 指令章节实现，接入目标项目并真机验证 | 进行中 |
| 3 | 手册其余章节（DISC / PUBEX / READ / will / SSL / aliauth） | 待开始 |

> 库中凡标注「实测」的约束，均来自目标项目上的真机复现；
> 尚未在真机验证过的章节会在其头文件中显式标注。
