/*
 * lmqtt_pub.h - AT+LMQTTPUB 发布消息
 *
 * 手册最大响应时间 30 秒，受网络状态影响。
 * 本文件实现「携带数据的 inline 模式」；透传模式（不带 <msg>，等待 '>' 提示符
 * 后分段装载；手册：数据长度不超过 8000 字节）用于超过 LMQTT_PUB_INLINE_MAX
 * 的报文，属后续阶段。
 */
#ifndef LMQTT_PUB_H
#define LMQTT_PUB_H 1

#include "lmqtt_types.h"
#include "lmqtt_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 手册：在携带数据的模式时，有效数据长度范围 0-1024。（当携带双引号的字符串
   格式时不超过 1027 字节长度，其他模式下不超过 1024 字节）
   本库以带引号字符串发布，上限保守取 1024。 */
#define LMQTT_PUB_INLINE_MAX    1024U

/*
 * 发布消息。
 *   msgid       1~65535，结果 URC 会回带该值用于配对
 *   qos         QoS 等级（0/1/2）
 *   retain      服务器是否保留该消息
 *   topic       主题，最长 LMQTT_TOPIC_MAX 字节
 *   payload/len 消息内容；len 不得超过 LMQTT_PUB_INLINE_MAX（见上方手册说明）
 *   timeout_ms  等待结果 URC 的毫秒数；传 0 使用 LMQTT_TMO_PUB
 *
 * 判定：<result> 为 0（已收到服务器 ACK）或 1（手册：发送成功了，但响应错误 ACK）
 *      均视为成功；2 返回 LMQTT_ERR_RESULT。
 *
 * 注意：payload 会被逐字节原样发出（不做转义）。发布 JSON 这类含引号的内容
 *       在实测中可正常送达，无需额外处理。
 */
int32_t lmqtt_pub(lmqtt_t *self, uint16_t msgid, lmqtt_qos_t qos, bool retain,
                  const char *topic, const void *payload, size_t len,
                  uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* LMQTT_PUB_H */
