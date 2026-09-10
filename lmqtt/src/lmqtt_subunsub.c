/*
 * lmqtt_subunsub.c - AT+LMQTTSUBUNSUB
 */
#include "lmqtt_subunsub.h"
#include "lmqtt_internal.h"

#include <stdio.h>
#include <string.h>

static int32_t subunsub(lmqtt_t *me, uint8_t subflag, uint16_t msgid,
                        const char *topic, lmqtt_qos_t qos)
{
    char            cmd[LMQTT_LINE_MAX];
    lmqtt_cmd_out_t out = { 0 };
    int32_t         rc;

    if (me == NULL || topic == NULL) {
        return LMQTT_ERR_PARAM;
    }
    if (msgid == 0) {
        return LMQTT_ERR_PARAM;             /* 手册：msgID 1~65535 */
    }
    if (strlen(topic) > LMQTT_TOPIC_MAX) {
        return LMQTT_ERR_OVERFLOW;
    }

    rc = snprintf(cmd, sizeof(cmd), "AT+LMQTTSUBUNSUB=%u,%u,%u,\"%s\",%u",
                  (unsigned)me->tcid, (unsigned)subflag, (unsigned)msgid,
                  topic, (unsigned)qos);
    if (rc < 0 || rc >= (int)sizeof(cmd)) {
        return LMQTT_ERR_OVERFLOW;
    }

    rc = lmqtt_cmd_exec(me, LMQTT_CMD_SUBUNSUB, msgid,
                        LMQTT_TMO_ACK, LMQTT_TMO_SUBUNSUB, &out, cmd);
    if (rc != LMQTT_OK) {
        return rc;
    }

    /* 订阅/退订的 result：0=成功，1=失败 */
    return (out.result == 0) ? LMQTT_OK : LMQTT_ERR_RESULT;
}

int32_t lmqtt_subscribe(lmqtt_t *me, uint16_t msgid,
                        const char *topic, lmqtt_qos_t qos)
{
    return subunsub(me, 0, msgid, topic, qos);
}

int32_t lmqtt_unsubscribe(lmqtt_t *me, uint16_t msgid, const char *topic)
{
    /* 退订时 qos 参数被模组忽略 */
    return subunsub(me, 1, msgid, topic, LMQTT_QOS0);
}
