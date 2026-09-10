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
 * 执行一条 AT 命令。
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

/* 向下行缓冲投递一条 payload（由 RECV URC 调用）。data 不要求以 '\0' 结尾。 */
void lmqtt_downlink_put(lmqtt_t *me, const char *data, size_t len);

/* STATS URC 分发（由 core 的行分发调用） */
void lmqtt_stats_dispatch(lmqtt_t *me, lmqtt_stats_t stat, int32_t extend);

#ifdef __cplusplus
}
#endif

#endif /* LMQTT_INTERNAL_H */
