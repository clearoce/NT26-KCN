/*
 * lmqtt_types.h - LMQTT 公共类型、错误码、常量
 *
 * 取值依据《NT26 MQTT 协议指令》手册；凡手册未明说但实测得出的约束，
 * 均在注释中标注「实测」。
 */
#ifndef LMQTT_TYPES_H
#define LMQTT_TYPES_H 1

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* 库身份与版本

   符号前缀保持 lmqtt_（短前缀 + 头文件目录隔离），完整身份由这组宏承担：
   需要强唯一标识的场合（版本检查、条件编译、包管理、文档索引）用它们。 */
#define NT26_KCN_LMQTT_VERSION_MAJOR    0
#define NT26_KCN_LMQTT_VERSION_MINOR    1
#define NT26_KCN_LMQTT_VERSION_PATCH    0
#define NT26_KCN_LMQTT_VERSION_STRING   "0.1.0"

/* ------------------------------------------------------------------ */
/* 错误码（负值）

   与 AT 层的 ERROR 区分开：模组回 ERROR/+CME ERROR 归一为 LMQTT_ERR_AT。 */
#define LMQTT_OK            0
#define LMQTT_ERR_PARAM     (-1)    /* 入参非法 */
#define LMQTT_ERR_TIMEOUT   (-2)    /* 等待响应/URC 超时 */
#define LMQTT_ERR_AT        (-3)    /* 模组返回 ERROR / +CME ERROR */
#define LMQTT_ERR_RESULT    (-4)    /* 结果 URC 的 <result> 非成功值 */
#define LMQTT_ERR_NOMEM     (-5)    /* 移植层资源创建失败 */
#define LMQTT_ERR_STATE     (-6)    /* 当前状态不允许该操作 */
#define LMQTT_ERR_OVERFLOW  (-7)    /* 数据超出缓冲/长度上限 */
#define LMQTT_ERR_IO        (-8)    /* 移植层写入失败 */

/* ------------------------------------------------------------------ */
/* 超时（毫秒）—— 取自手册各指令的「最大响应时间」 */
#define LMQTT_TMO_CFG       5000U
#define LMQTT_TMO_OPEN      160000U
#define LMQTT_TMO_CONN      30000U
#define LMQTT_TMO_SUBUNSUB  30000U
#define LMQTT_TMO_PUB       30000U
#define LMQTT_TMO_PUBEX     30000U
#define LMQTT_TMO_READ      5000U
#define LMQTT_TMO_CLOSE     5000U
#define LMQTT_TMO_DISC      5000U

/* 命令被接受（OK/ERROR）的等待窗口。手册未规定，实测约 0.5s */
#define LMQTT_TMO_ACK       5000U

/* ------------------------------------------------------------------ */
/* 容量上限 */
#define LMQTT_LINE_MAX      512U    /* 单行 AT 响应/URC 组帧缓冲 */
#define LMQTT_TOPIC_MAX     256U    /* 手册：topic 0~256 字节 */
#define LMQTT_DOWN_MAX      512U    /* 下行 payload 缓冲 */
#define LMQTT_HOST_MAX      100U    /* 手册：host_name 最大 100 字节 */
#define LMQTT_CLIENTID_MAX  256U    /* 手册：clientID/username 最大 256 字节 */

/* 手册：tcpconnectID 0~4，目前仅支持 0 */
#define LMQTT_TCID_DEFAULT  0U

/* ------------------------------------------------------------------ */
/* QoS（手册 <qos>） */
typedef enum lmqtt_qos {
    LMQTT_QOS0 = 0,     /* 最多一次，无需接收端确认 */
    LMQTT_QOS1 = 1,     /* 至少一次，需接收端回复 ACK */
    LMQTT_QOS2 = 2,     /* 正好一次 */
} lmqtt_qos_t;

/* 通用 <result>（OPEN/CLOSE/CONN/SUBUNSUB/PUB/DISC 共用语义） */
typedef enum lmqtt_result {
    LMQTT_RES_OK        = 0,    /* 成功；QoS1/2 时表示已收到服务器 ACK */
    LMQTT_RES_RETRANS   = 1,    /* 数据包重传（实测：消息已到达 broker，非失败） */
    LMQTT_RES_FAIL      = 2,    /* 发送失败 */

    /* SUBUNSUB 的 result 只有 0/1，1 表示失败 */
    LMQTT_SUB_RES_OK    = 0,
    LMQTT_SUB_RES_FAIL  = 1,
} lmqtt_result_t;

/* OPEN 失败时的 <extend>（手册 result=-1 时） */
typedef enum lmqtt_open_ext {
    LMQTT_OPEN_EXT_PARAM    = 1,    /* 参数错误 */
    LMQTT_OPEN_EXT_IN_USE   = 2,    /* MQTT 标识符被占用 */
    LMQTT_OPEN_EXT_PDP      = 3,    /* 激活 PDP 失败 */
    LMQTT_OPEN_EXT_DNS      = 4,    /* 域名解析失败 */
    LMQTT_OPEN_EXT_NET_DOWN = 5,    /* 网络断开 */
} lmqtt_open_ext_t;

/* SUBUNSUB 的 <extend> */
typedef enum lmqtt_sub_ext {
    LMQTT_SUB_EXT_OK        = 1,    /* 订阅成功 */
    LMQTT_SUB_EXT_SEND_FAIL = 6,    /* 数据包发送失败 */
    LMQTT_SUB_EXT_PARAM     = 7,    /* 参数错误 */
} lmqtt_sub_ext_t;

/* CONN 的 <ret_code>（服务器拒绝原因） */
typedef enum lmqtt_conn_rc {
    LMQTT_CONN_ACCEPTED         = 0,    /* 接受连接 */
    LMQTT_CONN_BAD_PROTOCOL     = 1,    /* 不接受的协议版本 */
    LMQTT_CONN_ID_REJECTED      = 2,    /* 标识符被拒绝 */
    LMQTT_CONN_SERVER_UNAVAIL   = 3,    /* 服务器不可用 */
    LMQTT_CONN_BAD_CREDENTIAL   = 4,    /* 错误的用户名或密码 */
    LMQTT_CONN_UNAUTHORIZED     = 5,    /* 未授权 */
    LMQTT_CONN_SEND_FAILED      = 6,    /* 数据包发送失败 */
    LMQTT_CONN_PARAM_ERROR      = 7,    /* 参数错误 */
} lmqtt_conn_rc_t;

/* MQTT 连接状态（AT+LMQTTCONN? 的 <state>） */
typedef enum lmqtt_conn_state {
    LMQTT_STATE_INIT        = 1,    /* MQTT 初始化 */
    LMQTT_STATE_CONNECTING  = 2,    /* 正在连接 */
    LMQTT_STATE_CONNECTED   = 3,    /* 已连接成功 */
    LMQTT_STATE_DISCONNECTING = 4,  /* 正在断开 */
} lmqtt_conn_state_t;

/* +LMQTTURC: STATS 的 <stats>（链路层状态码，手册 0~15） */
typedef enum lmqtt_stats {
    LMQTT_STATS_OK           = 0,   /* 成功 */
    LMQTT_STATS_PEER_RESET   = 1,   /* 连接被服务器断开或重置 */
    LMQTT_STATS_URL_ERROR    = 2,   /* URL 解析出错 */
    LMQTT_STATS_DNS_FAIL     = 3,   /* DNS 解析失败 */
    LMQTT_STATS_PROTO_ERROR  = 4,   /* 协议错误 */
    LMQTT_STATS_HTTP_ERROR   = 7,   /* HTTP 错误码，见 extend */
    LMQTT_STATS_CONN_TIMEOUT = 8,   /* 连接超时 */
    LMQTT_STATS_CONN_ERROR   = 9,   /* 连接错误 */
    LMQTT_STATS_FATAL        = 10,  /* 连接致命错误 */
    LMQTT_STATS_CLOSED       = 11,  /* 连接关闭 */
    LMQTT_STATS_NEED_MORE    = 12,  /* 需要更多数据 */
    LMQTT_STATS_CACHE_OVF    = 13,  /* 缓存溢出错误 */
    LMQTT_STATS_SSL_FAIL     = 14,  /* SSL 失败 */
    LMQTT_STATS_MQTT_REFUSED = 15,  /* MQTT 连接已拒绝，原因见 extend */
} lmqtt_stats_t;

/* STATS=15 时 <extend> 表示的拒绝原因（同 CONN 的 ret_code 语义） */
typedef lmqtt_conn_rc_t lmqtt_refuse_reason_t;

#ifdef __cplusplus
}
#endif

#endif /* LMQTT_TYPES_H */
