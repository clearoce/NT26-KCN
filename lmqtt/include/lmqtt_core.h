/*
 * lmqtt_core.h - 实例与 AT 引擎
 *
 * 本文件承载「手册里没有、但独立库必需」的那部分：行组帧、URC 分发、
 * 命令-响应配对。各指令章节（cfg/open/pub/...）建立在这套引擎之上。
 *
 * 数据流：
 *   发送  宿主/业务任务  ──lmqtt_pub()等──> 引擎 ──port->write──> 串口
 *   接收  串口接收任务   ──lmqtt_rx_feed()──> 引擎组帧 ─┬─ 结果 URC  → 唤醒等待中的命令
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

    /* 透传：**没有**结果 URC，结束条件与 NONE 相同（收到 OK/ERROR 即返回），
       额外把窗口内的普通响应行收集给调用方（见 lmqtt_cmd_exec_raw）。

       ⚠️ 只能追加在末尾：它的值要进 pending 的 kind 位，改动会移动既有取值。 */
    LMQTT_CMD_RAW,
} lmqtt_cmd_kind_t;

/*
 * 命令失败的「阶段」。
 *
 * 存在的理由：三处**不同的**失败 —— 等受理超时、等结果 URC 超时、结果槽被别的
 * 配对字占着 —— 在返回值上**折叠成同一个** LMQTT_ERR_TIMEOUT，调用方分不出是哪
 * 一段坏的。三者的现场含义完全不同（本地段没通 / 已通但对面没完成 / 并发窗口），
 * 所以单列一个阶段值。
 *
 * ⚠️ 它只**记录**，不改变任何 rc 与日志。
 * 其余失败本来就有各自的 rc（模组 ERROR → LMQTT_ERR_AT；结果非成功值 →
 * LMQTT_ERR_RESULT），不需要靠 stage 区分，故不在此列。
 */
typedef enum lmqtt_cmd_stage {
    LMQTT_STAGE_NONE = 0,       /* 未失败 / 尚未执行过命令 */
    LMQTT_STAGE_ACK_TMO,        /* 第一步等受理超时 —— 模组没搭理（本地段嫌疑） */
    LMQTT_STAGE_URC_TMO,        /* 第二步等结果 URC 超时 —— 受理了但没完成（模组/空口嫌疑） */
    LMQTT_STAGE_STALE_SLOT,     /* 结果槽署着别的配对字 —— 本命令的结果始终没到 */
} lmqtt_cmd_stage_t;

/*
 * 命令等待上下文（由引擎维护，指令层只读）。
 *
 * 并发约束：本结构由**命令任务**写、**接收任务**读，两者之间没有互斥 ——
 * 接收侧不能取 self->mutex：cmd_begin 持锁直到 cmd_abort，而 cmd_finish 在
 * 持锁期间等待接收侧的 sem_give，接收侧再去 lock 必然死锁。所以配对信息
 * (kind, msgid) 必须能一次性原子读写：这里打包进单个 32 位字 pending，
 * 命令侧整字发布、接收侧整字快照。旧实现把 kind/msgid 拆成两个 volatile
 * 字段分别读写，接收侧会读到"新 kind + 旧 msgid"这种撕裂组合，从而把上
 * 一条命令的迟到 URC 当成当前命令的结果。
 *
 * 前提：目标平台对**自然对齐的 32 位访问必须是单拷贝原子的**（Cortex-M0+
 * 等单核 MCU 天然满足；多核或带 cache 的平台须自行做一致性维护）。
 * 详见 lmqtt_port.h 的上下文约束。
 *
 * 字段一律 volatile：每次访问都落到内存，不给编译器重排或缓存的机会。
 */
typedef struct lmqtt_cmd_ctx {
    volatile uint32_t pending;      /* 配对字：[23:16]=kind，[15:0]=msgid */
    volatile int32_t  result;       /* 结果 URC 的 <result> */
    volatile int32_t  extra;        /* <extend> 或 <ret_code> */
    volatile uint32_t delivered;    /* 结果槽里这一份 result/extra 属于哪个配对字 */
    volatile bool     got;          /* 结果 URC 已到达 */
    volatile bool     acked;        /* 命令已被接受（收到 OK） */
    volatile bool     rejected;     /* 命令被拒绝（ERROR / +CME ERROR） */

    /* 命令窗口标志：命令侧在 cmd_begin 置位、cmd_abort 清位，接收侧据此把一条
       OK/ERROR 分成"窗内的回执"与"窗外路过的行"（两类分别计数，见
       lmqtt_err_counters 的 stray_ 与 extra_ 两组计数）。

       **不能用 pending 代替** —— CFG 的配对字字面值就是 0（lmqtt_cfg.c 传
       kind=NONE、msgid=0），与 cmd_abort 写回的空闲态**值碰撞**，那种写法会把
       CFG 的整个窗口都当成窗外。

       注意 OK/ERROR 本身**不带命令标识**，AT 协议层就没法配对 ⇒ 这里只能
       回答"当时是否有窗口"，回答不了"这份回执属于哪条命令"。 */
    volatile bool     busy;

    /* 命令**已完整写出**（cmd_exec 里两次 send 都已返回，才会进入 cmd_finish）。

       它是**栅栏**：在"开窗"到"写出完成"之间到达的 OK —— 模组那时还没收到命令 ——
       **必然不是本命令的回执**，一律只计 `early_*`，**不置位、不放行信号量**。
       不加这道栅栏，一条外来的 OK 就能把一条根本没被受理的命令判成成功。

       ⚠️ 开窗条件依赖"send 返回 = 末字节已进寄存器"（当前 `IOT_UART_TX_INT = 0`，
       轮询发送）。**启用 TX_INT 后必须改判"TX 流空 / TC"**，否则窗口开早、真 OK 被挡。 */
    volatile bool     written;

    /* ---- 透传命令（LMQTT_CMD_RAW）的响应落点 ----
       缓冲由**调用方**提供（见 lmqtt_cmd_exec_raw），库自身不持有任何常驻缓冲
       —— 本实例是静态的，在这里放一块定长缓冲会直接从 RAM 里扣。

       装填顺序要紧：raw_out 必须在 pending 之前写好。接收侧是"先读 pending
       认出 RAW、再去写 raw_out"，反过来的话它会拿上一轮遗留的指针写。

       raw_len 既是当前总长、也是下一次写入位置。写入者是接收侧（单线程），
       读取者在窗口关闭之后 —— 那时不会再有写入者。故与 down_drops 等计数
       同类，不加 volatile。 */
    char             *raw_out;
    size_t            raw_outsz;
    size_t            raw_len;
    uint16_t          raw_lines;

    /* 最近一次命令的失败阶段（见 lmqtt_cmd_stage_t）。
       **刻意不被 cmd_abort 清掉** —— abort 在每条命令结束时都会跑，清掉就没法事后
       读"上一次为什么失败"。只在 cmd_begin 复位。 */
    volatile uint8_t  stage;
} lmqtt_cmd_ctx_t;

/* 配对字的编解码：kind 占高 8 位、msgid 占低 16 位，整体装得进一个字。 */
#define LMQTT_CMD_PEND(k, m)   (((uint32_t)(uint8_t)(k) << 16) | \
                                (uint32_t)(uint16_t)(m))
#define LMQTT_PEND_KIND(w)     ((lmqtt_cmd_kind_t)(((w) >> 16) & 0xFFu))
#define LMQTT_PEND_MSGID(w)    ((uint16_t)((w) & 0xFFFFu))

typedef struct lmqtt lmqtt_t;

/*
 * STATS 通知回调。
 * 上下文：lmqtt_rx_feed() 的调用者（通常是串口接收任务），必须尽快返回——
 *        不要在其中做解析、打印大段日志或申请内存。
 */
typedef void (*lmqtt_stats_cb_t)(lmqtt_t *self, lmqtt_stats_t stat,
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

    /* ---- 下行交付（内部，经 lmqtt_take_downlink 取走）----
       双缓冲，生产者在 RX 上下文、消费者在另一任务：
       单缓冲时 take() 一清 down_ready，RX 侧的下一条 URC 就会覆写消费者
       正在解析的那块内存（"借用语义"形同虚设）。改双缓冲后，生产者的
       下一次写落在另一块槽，再下一次会被 down_ready 拦下 —— 消费者手里的
       那块在整个解析期间都安全。 */
    char            down[2][LMQTT_DOWN_MAX];
    volatile uint8_t down_idx;      /* 生产者最后写入的槽号 */
    volatile bool   down_ready;
    uint32_t        down_drops;     /* 因未及时取走/超长而丢弃的条数 */

    /* ---- 故障计数（只增不减，业务侧差分取用，见 lmqtt_err_counters）----
       这几类事件原先只落一行日志、丢完即忘，业务侧取不到 ⇒ 无法成为判据。
       库不判定它们"是哪一类故障"（那是宿主的知识，例如同一份超长行在 OTA
       接收期与非接收期归属不同码），只负责如实计数。 */
    uint32_t        line_drops;     /* 组帧行超 LMQTT_LINE_MAX，整行丢弃 */
    uint32_t        unmatched_urcs; /* +LMQTT 开头但本引擎不认识的 URC */
    uint32_t        bad_pub_acks;   /* PUB 的 <result>=1：发送成功但响应错误 ACK */

    /* ---- 回执归属的计量（见 lmqtt_cmd_ctx_t.busy）----
       这六项只增不减、由接收侧（lmqtt_rx_feed 的调用者）累加，业务侧差分取用。
       它们回答的不是"哪条回执串了门"（那做不到），而是"窗外漏进多少、窗内多来多少"
       —— 够不够频繁，是决定要不要做窗口栅栏的唯一依据。 */
    uint32_t        stray_oks;      /* 无窗口在途时到达的 OK */
    uint32_t        stray_errs;     /* 无窗口在途时到达的 ERROR / +CME ERROR */
    uint32_t        extra_acks;     /* 同一窗口内第 2 条及以后的 OK */
    uint32_t        extra_errs;     /* 同一窗口内第 2 条及以后的 ERROR */
    uint32_t        early_acks;     /* 命令尚未完整写出时到达的 OK（必非本命令的） */
    uint32_t        early_errs;     /* 同上，ERROR / +CME ERROR */
    uint32_t        stale_urcs;     /* 配对字不符、被丢弃的结果 URC */
    uint32_t        rx_other;       /* 既非 +LMQTT 也非 OK/ERROR 的行（诊断用，非故障） */
    /* `+CME ERROR: <n>` 的**编号**。此前只做前缀文本匹配就把整行丢了 ——
       而编号是模组对"这条命令为什么不认"的唯一说明（见 test_* 与 AT 手册）。 */
    uint32_t        cme_errs;       /* 收到过多少条 +CME ERROR */
    int32_t         last_cme_code;  /* 最近一条的编号；0 = 从未收到过 */

    /* 模组的 `NORMAL POWER DOWN` 通告。**它是"复位真的生效了"的唯一证据** ——
       诊断拉过 PWRKEY 之后，此前只能靠"裸 AT 还能应答"来判，而"从没真正断过电"
       同样满足那一条。模组主动吐出这一行，才说明它确实断电了。 */
    uint32_t        pwr_downs;      /* 收到过多少条 NORMAL POWER DOWN */
    /* 模组的**启动横幅**。与上面那条是一对：断电通告说"我断了"，横幅说"我起来了"。
       两条凑齐才算一次完整的复位。文本取自实测 RTT 抓包（`rx: ^boot.rom'v`），
       与 lmqtt/tests/test_core.c 里用作"模组启动横幅"样本的那一行一致。 */
    uint32_t        boots;          /* 收到过多少条启动横幅 */

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
int32_t lmqtt_init(lmqtt_t *self, const lmqtt_port_t *port);

/* 释放 init 创建的同步原语。调用后实例不可再用。 */
void lmqtt_deinit(lmqtt_t *self);

/*
 * 喂入串口收到的原始字节。
 *
 * 上下文：**串口接收任务**，不可在中断里调用。
 * 原因：内部会调用 port->sem_give / stats_cb / LMQTT_LOG，移植层提供的都是
 * 任务级原语（FreeRTOS 下即 xSemaphoreGive 等）。要在 ISR 里用，得先在宿主
 * 侧把字节投进队列/流缓冲，由接收任务取出后再调用本函数 —— 本项目就是这么
 * 做的（iot_uart_recv 内回调）。
 *
 * 内部按 CRLF 组行，分发 URC / 唤醒命令等待。可逐字节多次调用。
 * 返回 LMQTT_OK；入参非法返回 LMQTT_ERR_PARAM。
 */
int32_t lmqtt_rx_feed(lmqtt_t *self, const void *buf, size_t len);

/*
 * 取走一条下行 payload（业务任务轮询调用），无数据返回 NULL。
 * 返回的指针在下次调用前有效（借用语义）。
 *
 * 之所以设计成轮询而不是回调：下发 URC 的到达上下文往往是栈很小的接收任务，
 * 把解析（如 cJSON）压在那里会爆栈——本项目在旧实现上已实际复现过此类死机。
 * 解析应当在调用本函数那个任务自己的栈上完成。
 */
const char *lmqtt_take_downlink(lmqtt_t *self);

/* 累计被丢弃的下行条数（未及时取走或超出 LMQTT_DOWN_MAX） */
uint32_t lmqtt_downlink_drops(const lmqtt_t *self);

/* 故障计数快照。四个计数只增不减，业务侧每轮取一次做差分即可得到"本轮新增几条"。
   做成"一次取走一组"而不是四个访问器：四个计数的用途与采样节奏完全相同，
   分开取只会让调用侧把同一段差分样板抄四遍。 */
typedef struct lmqtt_err_counters {
    uint32_t down_drops;        /* 下行收到但丢弃 */
    uint32_t line_drops;        /* 组帧行超长被整行丢弃 */
    uint32_t unmatched_urcs;    /* 不认识的 +LMQTT URC */
    uint32_t bad_pub_acks;      /* PUB <result>=1 */

    /* ---- 回执归属的计量（2026-09-30 追加）----
       AT 的 OK/ERROR 不带命令标识，所以"这份回执属于哪条命令"在协议层就做不到，
       只能计量。stray_* 与 extra_* 一起回答"窗外漏进多少、窗内多来多少"。
       rx_other 是诊断用计数（durian 的 AT 响应行都会落进这里），**不是故障**。 */
    uint32_t stray_oks;         /* 无窗口在途时到达的 OK */
    uint32_t stray_errs;        /* 无窗口在途时到达的 ERROR / +CME ERROR */
    uint32_t extra_acks;        /* 同一窗口内第 2 条及以后的 OK */
    uint32_t extra_errs;        /* 同一窗口内第 2 条及以后的 ERROR */
    uint32_t early_acks;        /* 命令尚未完整写出时到达的 OK（必非本命令的） */
    uint32_t early_errs;        /* 同上，ERROR / +CME ERROR */
    uint32_t stale_urcs;        /* 配对字不符、被丢弃的结果 URC */
    uint32_t rx_other;          /* 既非 +LMQTT 也非 OK/ERROR 的行 */
    uint32_t cme_errs;          /* +CME ERROR 的行数 */
    int32_t  last_cme_code;     /* 最近一条 +CME ERROR 的编号（0 = 从未收到） */
    uint32_t pwr_downs;         /* NORMAL POWER DOWN 的行数（模组确认断电） */
    uint32_t boots;             /* 启动横幅的行数（模组重启了） */
} lmqtt_err_counters_t;

void lmqtt_get_err_counters(const lmqtt_t *self, lmqtt_err_counters_t *out);

/* 最近一次命令的失败阶段（见 lmqtt_cmd_stage_t）。
   用途：把"超时"这个粗类拆成"本地段没通 / 已通但没完成 / 并发窗口"三选一。
   读到的值在**下一条命令开始**时才变，可以事后读。self 为 NULL 返回 LMQTT_STAGE_NONE。 */
uint8_t lmqtt_last_cmd_stage(const lmqtt_t *self);

/* 最近一条命令的 <result> / <extend>（结果 URC 的原始值）。
   为什么需要：SUB / PUB 等命令各自把内部的 out 吃掉后只返回一个 rc，
   <extend>（手册给了细分原因）就此丢失 —— 而同一 rc 下不同 extend 的现场动作
   完全不同（例：PUB 的 6 = 数据包发送失败，查链路/模组；7 = 参数错误，查固件配置）。
   语义：**只有结果 URC 真的投递过**才有值；没投递时 result 为 -1、extra 为 0
   （cmd_begin 的复位值）⇒ "超时"与"模组真的回了 -1"不会混淆。
   ⚠️ 属"最近一次"语义 —— 要跟具体命令对应，须在该命令返回后**立刻**读
   （下一条命令的 begin 会复位它们）。 */
int32_t lmqtt_last_cmd_result(const lmqtt_t *self);
int32_t lmqtt_last_cmd_extra(const lmqtt_t *self);

/* 当前是否已连接（以 CONN 成功 / STATS 断线通知为准） */
bool lmqtt_is_connected(const lmqtt_t *self);

/* 注册 STATS 回调（可为 NULL 注销）。cb 为 NULL 时也可用 lmqtt_is_connected 轮询。 */
void lmqtt_set_stats_cb(lmqtt_t *self, lmqtt_stats_cb_t cb, void *user);

/*
 * 执行一条「透传」命令：写出 cmd（自动补 CRLF），等 OK/ERROR，
 * 并把**窗口内**收到的普通响应行收集到调用方给出的缓冲。
 *
 * 存在的理由：AT 的 OK/ERROR 行**不带命令标识**，归属只能靠"当时是谁的窗口"
 * 判定。非 LMQTT 的 AT 命令（AT+CSQ / AT+CEREG? / AT+CGPADDR …）若另起一个
 * 引擎发送，它的收尾 OK 必然落在本引擎的窗外、被计成 stray —— 且反向也可能
 * 被那个引擎冒领。走本函数后**AT 通道上只剩一个发射源**，归属才有定义。
 *
 *   cmd        命令字符串，**不含** CRLF
 *   ack_tmo    等 OK/ERROR 的毫秒数
 *   out/outsz  响应行落点；可为 NULL（只等结果、不收集）
 *   out_len    回填总字节数；out_lines 回填行数（二者均可为 NULL）
 *
 * 各行以 '\0' 结尾、连续存放，行内**不含** CR/LF。缓冲放不下时**整行丢弃**
 * （截断的半行会被调用方误解析），不做部分写入。
 *
 * 返回值同 lmqtt_cmd_exec。⚠️ out 的内容在函数**返回时**才完整；失败路径上
 * 它可能停在半途，调用方应按返回值决定是否解析。
 */
int32_t lmqtt_cmd_exec_raw(lmqtt_t *self, const char *cmd, uint32_t ack_tmo,
                           char *out, size_t outsz,
                           size_t *out_len, size_t *out_lines);

#ifdef __cplusplus
}
#endif

#endif /* LMQTT_CORE_H */
