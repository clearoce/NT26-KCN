/*
 * lmqtt_open.c - AT+LMQTTOPEN
 */
#include "lmqtt_open.h"
#include "lmqtt_internal.h"

#include <stdio.h>
#include <string.h>

int32_t lmqtt_open(lmqtt_t *me, const char *host, uint16_t port,
                   lmqtt_open_ext_t *ext_out)
{
    char            cmd[LMQTT_LINE_MAX];
    lmqtt_cmd_out_t out = { 0 };
    int32_t         rc;

    if (me == NULL || host == NULL) {
        return LMQTT_ERR_PARAM;
    }
    if (strlen(host) > LMQTT_HOST_MAX) {
        return LMQTT_ERR_OVERFLOW;
    }

    if (ext_out != NULL) {
        *ext_out = (lmqtt_open_ext_t)0;
    }

    if (snprintf(cmd, sizeof(cmd), "AT+LMQTTOPEN=%u,\"%s\",%u",
                 (unsigned)me->tcid, host, (unsigned)port) >= (int)sizeof(cmd)) {
        return LMQTT_ERR_OVERFLOW;
    }

    rc = lmqtt_cmd_exec(me, LMQTT_CMD_OPEN, 0,
                        LMQTT_TMO_ACK, LMQTT_TMO_OPEN, &out, cmd);
    if (rc != LMQTT_OK) {
        return rc;
    }

    if (ext_out != NULL) {
        *ext_out = (lmqtt_open_ext_t)out.extra;
    }

    /* result: 0=打开成功，-1=失败（原因在 extend） */
    if (out.result != 0) {
        me->connected = false;
        return LMQTT_ERR_RESULT;
    }

    return LMQTT_OK;
}
