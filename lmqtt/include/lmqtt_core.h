/*
 * lmqtt_core.h - 实例与 AT 引擎
 *
 * 本文件承载「手册里没有、但独立库必需」的那部分：行组帧、URC 分发、
 * 命令-响应配对。各指令章节（cfg/open/pub/...）建立在这套引擎之上。
 *
 * 数据流：
 *   发送  宿主/业务任务  ──lmqtt_pub()等──> 引擎 ──port->write──> 串口
 *   接收  串口 ISR/任务  ──lmqtt_rx_feed()──> 引擎组帧 ─┬─ 结果 URC  → 唤醒等待中的命令
 *                                                       ├─ STATS    → stats_cb 回调
 *                                                       └─ RECV     → 拷入下行缓冲
 *   下行  业务任务  ──lmqtt_take_downlink()──> 取走 payload 自行解析
 */
#ifndef LMQTT_CORE_H
#define LMQTT_CORE_H 1

#include "lmqtt_types.h"
#include "lmqtt_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 结果 URC 类型（引擎内部据此匹配 +LMQTTxxx 上报） */
typedef enum lmqtt_cmd_kind {
    LMQTT_CMD_NONE = 0,
    LMQTT_CMD_OPEN,         /* +LMQTTOPEN:     <id>,<result>[,<extend>]   */
    LMQTT_CMD_CLOSE,        /* +LMQTTCLOSE:    <id>,<result>[,<extend>]   */
    LMQTT_CMD_CONN,         /* +LMQTTCONN:     <id>,<result>[,<ret_code>] */
    LMQTT_CMD_DISC,         /* +LMQTTDISC:     <id>,<result>[,<extend>]   */
    LMQTT_CMD_SUBUNSUB,     /* +LMQTTSUBUNSUB: <id>,<msgID>,<result>[,<extend>] */
    LMQTT_CMD_PUB,          /* +LMQTTPUB:      <id>,<msgID>,<result>[,<extend>] */
} lmqtt_cmd_kind_t;

/* 命令等待上下文（由引擎维护，指令层只读） */
typedef struct lmqtt_cmd_ctx {
    volatile uint8_t  kind;         /* lmqtt_cmd_kind_t */
    volatile uint16_t msgid;        /* 期望的 msgID；0 表示不校验（OPEN/CLOSE/CONN/DISC） */
    volatile int32_t  result;       /* 结果 URC 的 <result> */
    volatile int32_t  extra;        /* <extend> 或 <ret_code> */
    volatile bool     got;          /* 结果 URC 已到达 */
    volatile bool     acked;        /* 命令已被接受（收到 OK） */
    volatile bool     rejected;     /* 命令被拒绝（ERROR / +CME ERROR） */
} lmqtt_cmd_ctx_t;

typedef struct lmqtt lmqtt_t;

/*
 * STATS 通知回调。
 * 上下文：lmqtt_rx_feed() 的调用者（通常是串口接收任务/中断），必须尽快返回——
 *        不要在其中做解析、打印大段日志或申请内存。
 */
typedef void (*lmqtt_stats_cb_t)(lmqtt_t *me, lmqtt_stats_t stat,
                                 int32_t extend, void *user);

struct lmqtt {
    const lmqtt_port_t *port;
    void               *user;

    /* ---- 同步原语（init 时创建）---- */
    lmqtt_mutex_t   mutex;          /* 串行化命令事务；port 未提供时为 NULL */
    lmqtt_sem_t     sem;            /* 命令完成通知 */

    /* ---- 行组帧（内部）---- */
    char            line[LMQTT_LINE_MAX];
    size_t          line_len;
    bool            line_over;      /* 当前行已溢出，整行将被丢弃 */

    /* ---- 当前命令（内部）---- */
    lmqtt_cmd_ctx_t cmd;

    /* ---- 下行交付（内部，经 lmqtt_take_downlink 取走）---- */
    char            down[LMQTT_DOWN_MAX];
    volatile bool   down_ready;
    uint32_t        down_drops;     /* 因未及时取走/超长而丢弃的条数 */

    /* ---- 状态 ---- */
    bool            connected;
    uint8_t         tcid;           /* tcpconnectID，固定 LMQTT_TCID_DEFAULT */

    /* ---- 回调 ---- */
    lmqtt_stats_cb_t stats_cb;
};

/*
 * 初始化实例。port 必须在实例生命周期内保持有效（通常为静态常量）。
 * 本函数会通过 port 创建互斥与信号量，失败返回 LMQTT_ERR_NOMEM。
 */
int32_t lmqtt_init(lmqtt_t *me, const lmqtt_port_t *port);

/* 释放 init 创建的同步原语。调用后实例不可再用。 */
void lmqtt_deinit(lmqtt_t *me);

/*
 * 喂入串口收到的原始字节（在串口 ISR 或接收任务里调用）。
 * 内部按 CRLF 组行，分发 URC / 唤醒命令等待。可逐字节多次调用。
 * 返回 LMQTT_OK；入参非法返回 LMQTT_ERR_PARAM。
 */
int32_t lmqtt_rx_feed(lmqtt_t *me, const void *buf, size_t len);

/*
 * 取走一条下行 payload（业务任务轮询调用），无数据返回 NULL。
 * 返回的指针在下次调用前有效（借用语义）。
 *
 * 之所以设计成轮询而不是回调：下发 URC 的到达上下文往往是栈很小的接收任务，
 * 把解析（如 cJSON）压在那里会爆栈——本项目在旧实现上已实际复现过此类死机。
 * 解析应当在调用本函数那个任务自己的栈上完成。
 */
const char *lmqtt_take_downlink(lmqtt_t *me);

/* 累计被丢弃的下行条数（未及时取走或超出 LMQTT_DOWN_MAX） */
uint32_t lmqtt_downlink_drops(const lmqtt_t *me);

/* 当前是否已连接（以 CONN 成功 / STATS 断线通知为准） */
bool lmqtt_is_connected(const lmqtt_t *me);

/* 注册 STATS 回调（可为 NULL 注销）。cb 为 NULL 时也可用 lmqtt_is_connected 轮询。 */
void lmqtt_set_stats_cb(lmqtt_t *me, lmqtt_stats_cb_t cb, void *user);

#ifdef __cplusplus
}
#endif

#endif /* LMQTT_CORE_H */
