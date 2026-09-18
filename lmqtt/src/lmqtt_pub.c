/*
 * lmqtt_pub.c - AT+LMQTTPUB（inline 模式）
 */
#include "lmqtt_pub.h"
#include "lmqtt_internal.h"

#include <stdio.h>
#include <string.h>

int32_t lmqtt_pub(lmqtt_t *self, uint16_t msgid, lmqtt_qos_t qos, bool retain,
                  const char *topic, const void *payload, size_t len,
                  uint32_t timeout_ms)
{
    char            head[LMQTT_LINE_MAX];
    lmqtt_cmd_out_t out = { 0 };
    int32_t         rc;

    if (self == NULL || topic == NULL || (payload == NULL && len > 0)) {
        return LMQTT_ERR_PARAM;
    }
    if (msgid == 0) {
        return LMQTT_ERR_PARAM;
    }
    if (strlen(topic) > LMQTT_TOPIC_MAX) {
        return LMQTT_ERR_OVERFLOW;
    }
    if (len > LMQTT_PUB_INLINE_MAX) {
        /* 超 inline 上限（本库取 1024；手册允许带引号字符串到 1027），需走透传模式 */
        return LMQTT_ERR_OVERFLOW;
    }

    /* 命令头以 '\"' 结尾，payload 紧随其后、最后补上闭合引号。
       分段发送是为了让大 payload 不必先整体拼进栈缓冲。 */
    rc = snprintf(head, sizeof(head), "AT+LMQTTPUB=%u,%u,%u,%u,\"%s\",%u,\"",
                  (unsigned)self->tcid, (unsigned)msgid, (unsigned)qos,
                  (unsigned)(retain ? 1 : 0), topic, (unsigned)len);
    if (rc < 0 || rc >= (int)sizeof(head)) {
        return LMQTT_ERR_OVERFLOW;
    }

    rc = lmqtt_cmd_begin(self, LMQTT_CMD_PUB, msgid);
    if (rc != LMQTT_OK) {
        return rc;
    }

    rc = lmqtt_cmd_send_str(self, head);
    if (rc == LMQTT_OK && len > 0) {
        rc = lmqtt_cmd_send(self, payload, len);
    }
    if (rc == LMQTT_OK) {
        rc = lmqtt_cmd_send(self, "\"\r\n", 3);
    }
    if (rc != LMQTT_OK) {
        lmqtt_cmd_abort(self);
        return rc;
    }

    rc = lmqtt_cmd_finish(self, LMQTT_TMO_ACK,
                          (timeout_ms > 0) ? timeout_ms : LMQTT_TMO_PUB, &out);
    if (rc != LMQTT_OK) {
        return rc;
    }

    /* result（手册）：0=数据包发送成功且接收到服务器的ACK（当 <qos>=0 时发布了数据，
       则无需ACK）；1=发送成功了，但是响应错误ACK；2=发送失败。
       <extend>（手册：当 <result>=1 时显示扩展错误信息）：6=数据包发送失败、7=参数错误。 */
    if (out.result != 0 && out.result != 1) {
        return LMQTT_ERR_RESULT;
    }
    return LMQTT_OK;
}
