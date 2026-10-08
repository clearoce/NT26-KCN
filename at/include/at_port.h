/*
 * at_port.h - 移植层接口
 *
 * 本模块**不引用任何 OS / MCU 头文件**，全部平台能力通过 at_port_t 注入 ——
 * 与 lmqtt 同一规矩（见 lmqtt/include/lmqtt_port.h）。
 *
 * 注入的能力分四类：内存、接收任务、AT 通道、时间。
 * ⚠️ 早期版本还注入互斥、信号量、时钟与"解析任务" —— at 层不再自行收发命令
 * 之后，它们就没有所有者了：命令事务的唯一所有者在 AT 通道引擎里。
 *
 * ── 移植层怎么注册 ──
 * 经 at_set_port() 全局注册，**必须在 at_client_init() 之前调用**：
 *
 *     at_set_port(&my_port);
 *     at_client_init(&client, ...);
 *
 * 支持多实例的收益在本项目为零，故不做实例级的 port。
 */
#ifndef AT_PORT_H
#define AT_PORT_H 1

#include "at_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 日志级别 */
typedef enum at_log_level {
    AT_LOG_ERROR = 0,
    AT_LOG_WARN  = 1,
    AT_LOG_INFO  = 2,
    AT_LOG_DEBUG = 3,
} at_log_level_t;

/*
 * 宿主配置头（可选）。
 *
 * 库不预设任何平台，宿主的日志/断言实现通过「配置文件」注入 —— 与本项目
 * 其它库同一套路（FreeRTOSConfig.h / ALUMY_CONFIG_FILE / LMQTT_CONFIG_FILE）。
 * 用法是在构建系统里定义：
 *     AT_CONFIG_FILE=<my_at_config.h>
 */
#ifdef AT_CONFIG_FILE
#include AT_CONFIG_FILE
#endif

/*
 * 日志接入（编译期，默认编译为空）：
 *     #define AT_LOG_IMPL(lv, fmt, ...)  my_log(lv, "[AT] " fmt, ##__VA_ARGS__)
 *
 * 注意实现里不要做重活：AT_LOG 可能在接收任务（小栈）里被调用。
 */
#ifdef AT_LOG_IMPL
#define AT_LOG(lvl, fmt, ...)    AT_LOG_IMPL(lvl, fmt, ##__VA_ARGS__)
#else
#define AT_LOG(lvl, fmt, ...)    ((void)0)
#endif

/*
 * 断言接入（编译期，默认展成 (void)0）。
 *     #define AT_ASSERT_IMPL(x)  my_assert(x, __FILE__, __LINE__)
 */
#ifdef AT_ASSERT_IMPL
#define AT_ASSERT(x)    AT_ASSERT_IMPL(x)
#else
#define AT_ASSERT(x)    ((void)0)
#endif

/* 句柄类型：由移植层 create 返回，库只当作不透明指针传递 */
typedef void *at_task_t;

typedef struct at_port {
    /* ---- 内存：响应缓冲、client 对象、发送缓冲 ---- */
    void *(*malloc)(size_t size);
    void  (*free)(void *p);

    /* ---- 任务：接收搬运。返回 NULL 表示创建失败 ---- */
    at_task_t (*task_create)(const char *name, uint32_t prio, size_t stack_words,
                             void (*entry)(void *), void *arg);

    /* ---- AT 通道：交给**唯一的 AT 引擎**（本项目为 lmqtt）----
       at 层不再自己组帧、也不再做命令-响应配对。AT 的 OK/ERROR 行**不带命令
       标识**，只有当整条通道上只有一个发射源时，"这份回执归谁"才有定义 ——
       两个引擎各自组帧时，一方的收尾 OK 必然出现在另一方的窗外。

       cmd_exec_raw：写出 cmd（**不含** CRLF，由引擎补）、等 OK/ERROR，并把窗口
         内的普通响应行按 '\0' 分隔写进 out（行内不含 CR/LF）。
         返回 AT_OK / AT_ERR_*。
       rx_feed：把串口收到的原始字节交给同一个引擎；at 层不再解析它们。 */
    int32_t (*cmd_exec_raw)(const char *cmd, uint32_t timeout_ms,
                            char *out, size_t outsz,
                            size_t *out_len, size_t *out_lines);
    void    (*rx_feed)(const void *buf, size_t len);

    /* ---- 时间 ---- */
    uint32_t (*get_tick)(void);
    uint32_t (*ms2tick)(uint32_t ms);
} at_port_t;

/* 注册移植层。必须在任何其它 at_* 调用之前执行一次。 */
void at_set_port(const at_port_t *port);

/* 取当前移植层（库内部用；未注册时返回 NULL） */
const at_port_t *at_get_port(void);

#ifdef __cplusplus
}
#endif

#endif /* AT_PORT_H */
