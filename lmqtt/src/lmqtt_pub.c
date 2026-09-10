/*
 * lmqtt_pub.c - AT+LMQTTPUB（inline 模式）
 */
#include "lmqtt_pub.h"
#include "lmqtt_internal.h"

#include <stdio.h>
#include <string.h>

int32_t lmqtt_pub(lmqtt_t *me, uint16_t msgid, lmqtt_qos_t qos, bool retain,
                  const char *topic, const void *payload, size_t len,
                  uint32_t timeout_ms)
{
    char            head[LMQTT_LINE_MAX];
    lmqtt_cmd_out_t out = { 0 };
    int32_t         rc;

    if (me == NULL || topic == NULL || (payload == NULL && len > 0)) {
        return LMQTT_ERR_PARAM;
    }
    if (msgid == 0) {
        return LMQTT_ERR_PARAM;
    }
    if (strlen(topic) > LMQTT_TOPIC_MAX) {
        return LMQTT_ERR_OVERFLOW;
    }
    if (len > LMQTT_PUB_INLINE_MAX) {
        return LMQTT_ERR_OVERFLOW;          /* 需走透传模式 */
    }

    /* 命令头以 '\"' 结尾，payload 紧随其后、最后补上闭合引号。
       分段发送是为了让大 payload 不必先整体拼进栈缓冲。 */
    rc = snprintf(head, sizeof(head), "AT+LMQTTPUB=%u,%u,%u,%u,\"%s\",%u,\"",
                  (unsigned)me->tcid, (unsigned)msgid, (unsigned)qos,
                  (unsigned)(retain ? 1 : 0), topic, (unsigned)len);
    if (rc < 0 || rc >= (int)sizeof(head)) {
        return LMQTT_ERR_OVERFLOW;
    }

    rc = lmqtt_cmd_begin(me, LMQTT_CMD_PUB, msgid);
    if (rc != LMQTT_OK) {
        return rc;
    }

    rc = lmqtt_cmd_send_str(me, head);
    if (rc == LMQTT_OK && len > 0) {
        rc = lmqtt_cmd_send(me, payload, len);
    }
    if (rc == LMQTT_OK) {
        rc = lmqtt_cmd_send(me, "\"\r\n", 3);
    }
    if (rc != LMQTT_OK) {
        lmqtt_cmd_abort(me);
        return rc;
    }

    rc = lmqtt_cmd_finish(me, LMQTT_TMO_ACK,
                          (timeout_ms > 0) ? timeout_ms : LMQTT_TMO_PUB, &out);
    if (rc != LMQTT_OK) {
        return rc;
    }

    /* result: 0=发送成功且收到服务器 ACK，1=重传（实测已到达），2=发送失败 */
    if (out.result != 0 && out.result != 1) {
        return LMQTT_ERR_RESULT;
    }
    return LMQTT_OK;
}
