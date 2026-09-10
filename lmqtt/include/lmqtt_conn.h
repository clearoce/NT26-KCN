/*
 * lmqtt_conn.h - AT+LMQTTCONN 连接 MQTT 客户端到服务器
 *
 * 手册最大响应时间 30 秒，受网络状态影响。
 * 实测约束：CONN 成功后没有静默期，会话补投的下行消息可能与本命令的
 * 结果 URC 同批到达，因此下行接收必须在此命令返回前就已就绪。
 */
#ifndef LMQTT_CONN_H
#define LMQTT_CONN_H 1

#include "lmqtt_types.h"
#include "lmqtt_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 连接服务器。
 *   clientid / username / password  按服务器要求填写；username 为 NULL 时只带 clientid
 *   rc_out  可为 NULL；回填服务器返回码（0=接受，其余为拒绝原因）
 *
 * 判定：<result> 为 0 或 1（重传，实测消息已到达）且 <ret_code> 为 0 才算成功。
 *      认证失败（ret_code=4）、未授权（5）等都会以 LMQTT_ERR_RESULT 返回。
 * 成功后实例转为已连接状态。
 */
int32_t lmqtt_conn(lmqtt_t *me, const char *clientid,
                   const char *username, const char *password,
                   lmqtt_conn_rc_t *rc_out);

#ifdef __cplusplus
}
#endif

#endif /* LMQTT_CONN_H */
