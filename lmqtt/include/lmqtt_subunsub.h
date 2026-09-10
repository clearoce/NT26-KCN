/*
 * lmqtt_subunsub.h - AT+LMQTTSUBUNSUB 订阅 / 取消订阅
 *
 * 手册最大响应时间 30 秒。
 */
#ifndef LMQTT_SUBUNSUB_H
#define LMQTT_SUBUNSUB_H 1

#include "lmqtt_types.h"
#include "lmqtt_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 订阅主题。
 *   msgid  1~65535，模组会在结果 URC 中回带，用于配对；调用方自行保证其唯一性
 *   qos    订阅 QoS；下行消息的 QoS 取「发布方 QoS」与「订阅 QoS」的较小值
 *
 * 判定：<result> 为 0 才算成功。
 */
int32_t lmqtt_subscribe(lmqtt_t *me, uint16_t msgid,
                        const char *topic, lmqtt_qos_t qos);

/*
 * 取消订阅。
 * 手册示例中退订的完成 URC 拼写为 +LMQTTUNSUNSUB（比订阅少一个 B），
 * 引擎已兼容两种拼写。
 */
int32_t lmqtt_unsubscribe(lmqtt_t *me, uint16_t msgid, const char *topic);

#ifdef __cplusplus
}
#endif

#endif /* LMQTT_SUBUNSUB_H */
