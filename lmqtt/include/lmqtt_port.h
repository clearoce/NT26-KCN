/*
 * lmqtt_port.h - 移植层接口
 *
 * 库不引用任何 OS / MCU 头文件，全部平台相关能力通过本结构注入。
 * 宿主只需实现 write + 一组互斥/信号量原语；串口接收方向由宿主在
 * **接收任务**里调用 lmqtt_rx_feed() 喂入（见 lmqtt_core.h）。
 *
 * 上下文约束：本结构里的信号量/互斥都是任务级原语，因此 lmqtt_rx_feed()
 * 也必须在任务上下文调用 —— 中断里只做"投递到队列/流缓冲"，不要直接调它。
 */
#ifndef LMQTT_PORT_H
#define LMQTT_PORT_H 1

#include "lmqtt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 日志级别 */
typedef enum lmqtt_log_level {
    LMQTT_LOG_ERROR = 0,
    LMQTT_LOG_WARN  = 1,
    LMQTT_LOG_INFO  = 2,
    LMQTT_LOG_DEBUG = 3,
} lmqtt_log_level_t;

/*
 * 宿主配置头（可选）。
 *
 * 库不预设任何平台，宿主的日志实现通过「配置文件」注入 —— 与本项目其它库
 * 同一套路（FreeRTOSConfig.h / ALUMY_CONFIG_FILE / LFS_CONFIG）。用法是在
 * 构建系统里定义：
 *     LMQTT_CONFIG_FILE=<my_lmqtt_config.h>
 * 该头会在日志宏展开之前被包含，宿主可在其中定义 LMQTT_LOG_IMPL。
 *
 * 之所以不用 -DLMQTT_LOG_IMPL=... ：那是个带 ... 的宏，命令行转义逗号很麻烦，
 * 而且库的多个 .c 都要看到它，逐个文件 #define 容易漏。
 */
#ifdef LMQTT_CONFIG_FILE
#include LMQTT_CONFIG_FILE
#endif

/*
 * 日志接入（编译期，默认编译为空）：
 * 宿主可在包含本头文件之前定义 LMQTT_LOG_IMPL 接入自己的日志系统，例如
 *     #define LMQTT_LOG_IMPL(lv, fmt, ...)  my_log(lv, "[LMQTT] " fmt, ##__VA_ARGS__)
 *
 * 注意实现里不要做重活：LMQTT_LOG 可能在串口接收上下文（小栈任务）里被调用。
 */
#ifdef LMQTT_LOG_IMPL
#define LMQTT_LOG(lvl, fmt, ...)    LMQTT_LOG_IMPL(lvl, fmt, ##__VA_ARGS__)
#else
#define LMQTT_LOG(lvl, fmt, ...)    ((void)0)
#endif

/* 句柄类型：由移植层 create 返回，库只当作不透明指针传递 */
typedef void *lmqtt_mutex_t;
typedef void *lmqtt_sem_t;

typedef struct lmqtt_port {
    /*
     * 串口输出。返回实际写出的字节数；负值表示失败。
     * 调用上下文：命令 API 的调用者任务（库已持有互斥）。
     */
    int32_t (*write)(const void *buf, size_t len);

    /* ---- 互斥：串行化一次完整的命令事务（可为 NULL，表示单任务调用）---- */
    lmqtt_mutex_t (*mutex_create)(void);
    void          (*mutex_lock)(lmqtt_mutex_t m);
    void          (*mutex_unlock)(lmqtt_mutex_t m);
    void          (*mutex_destroy)(lmqtt_mutex_t m);      /* 可为 NULL */

    /* ---- 二值信号量：命令完成 / 结果 URC 到达的通知 ---- */
    lmqtt_sem_t (*sem_create)(void);
    int32_t     (*sem_take)(lmqtt_sem_t s, uint32_t timeout_ms);  /* 0=取到，非 0=超时 */
    void        (*sem_give)(lmqtt_sem_t s);
    void        (*sem_reset)(lmqtt_sem_t s);
    void        (*sem_destroy)(lmqtt_sem_t s);            /* 可为 NULL */
} lmqtt_port_t;

#ifdef __cplusplus
}
#endif

#endif /* LMQTT_PORT_H */
