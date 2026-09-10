/*
 * lmqtt_internal.h - 库内部接口（不对外安装）
 *
 * 各指令章节文件（lmqtt_cfg.c / lmqtt_open.c / ...）通过这里调用 AT 引擎。
 */
#ifndef LMQTT_INTERNAL_H
#define LMQTT_INTERNAL_H 1

#include "lmqtt_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 结果 URC 携带的信息 */
typedef struct lmqtt_cmd_out {
    int32_t result;     /* <result> */
    int32_t extra;      /* <extend>（OPEN/CLOSE/DISC/SUBUNSUB/PUB）或 <ret_code>（CONN） */
} lmqtt_cmd_out_t;

/*
 * 执行一条 AT 命令（等价于 begin + send + finish；命令会自动补 CRLF）。
 *
 *   kind    期望的结果 URC 类型；LMQTT_CMD_NONE 表示只等 OK/ERROR（如 LMQTTCFG）
 *   msgid   期望的 msgID；0 表示不校验（OPEN/CLOSE/CONN/DISC 无 msgID 字段）
 *   ack_tmo 等待 OK/ERROR 的毫秒数
 *   urc_tmo 等待结果 URC 的毫秒数（kind 为 NONE 时忽略）
 *   out     非 NULL 时回填 <result> / <extend>
 *
 * 返回值：LMQTT_OK / LMQTT_ERR_TIMEOUT / LMQTT_ERR_AT / LMQTT_ERR_PARAM
 * 注意：本函数不判定 <result> 的业务含义（各指令语义不同），由调用方判断。
 */
int32_t lmqtt_cmd_exec(lmqtt_t *me, lmqtt_cmd_kind_t kind, uint16_t msgid,
                       uint32_t ack_tmo, uint32_t urc_tmo,
                       lmqtt_cmd_out_t *out, const char *cmd);

/*
 * 分段执行：当一条命令的数据部分大到不适合先在栈上拼好时使用
 * （如 LMQTTPUB 的 payload）。三步必须成对出现，中间可多次 send。
 *
 *   lmqtt_cmd_begin()  —— 加锁并复位命令上下文
 *   lmqtt_cmd_send()   —— 发送原始字节（不补 CRLF）
 *   lmqtt_cmd_finish() —— 等 OK/URC、解锁，返回结果
 *
 * 任一步失败后应直接返回；finish 会负责解锁。
 */
int32_t lmqtt_cmd_begin(lmqtt_t *me, lmqtt_cmd_kind_t kind, uint16_t msgid);
int32_t lmqtt_cmd_send(lmqtt_t *me, const void *buf, size_t len);
int32_t lmqtt_cmd_finish(lmqtt_t *me, uint32_t ack_tmo, uint32_t urc_tmo,
                         lmqtt_cmd_out_t *out);
/* 放弃事务（发送中途出错时调用），负责解锁 */
int32_t lmqtt_cmd_abort(lmqtt_t *me);

/* 向串口写字符串（不补 CRLF），供分段发送拼装用 */
int32_t lmqtt_cmd_send_str(lmqtt_t *me, const char *s);

/* 向下行缓冲投递一条 payload（由 RECV URC 调用）。data 不要求以 '\0' 结尾。 */
void lmqtt_downlink_put(lmqtt_t *me, const char *data, size_t len);

/* STATS URC 分发（由 core 的行分发调用） */
void lmqtt_stats_dispatch(lmqtt_t *me, lmqtt_stats_t stat, int32_t extend);

#ifdef __cplusplus
}
#endif

#endif /* LMQTT_INTERNAL_H */
