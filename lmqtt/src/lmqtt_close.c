/*
 * lmqtt_close.c - AT+LMQTTCLOSE
 */
#include "lmqtt_close.h"
#include "lmqtt_internal.h"

#include <stdio.h>

int32_t lmqtt_close(lmqtt_t *me)
{
    char            cmd[32];
    lmqtt_cmd_out_t out = { 0 };
    int32_t         rc;

    if (me == NULL) {
        return LMQTT_ERR_PARAM;
    }

    snprintf(cmd, sizeof(cmd), "AT+LMQTTCLOSE=%u", (unsigned)me->tcid);

    rc = lmqtt_cmd_exec(me, LMQTT_CMD_CLOSE, 0,
                        LMQTT_TMO_ACK, LMQTT_TMO_CLOSE, &out, cmd);

    /* 无论模组怎么回，本地都不该再认为自己连着 */
    me->connected = false;

    if (rc != LMQTT_OK) {
        return rc;
    }
    /* result: 0=成功，1=失败 */
    return (out.result == 0) ? LMQTT_OK : LMQTT_ERR_RESULT;
}
