/*
 * lmqtt_conn.c - AT+LMQTTCONN
 */
#include "lmqtt_conn.h"
#include "lmqtt_internal.h"

#include <stdio.h>
#include <string.h>

int32_t lmqtt_conn(lmqtt_t *me, const char *clientid,
                   const char *username, const char *password,
                   lmqtt_conn_rc_t *rc_out)
{
    char            cmd[LMQTT_LINE_MAX];
    lmqtt_cmd_out_t out = { 0 };
    int32_t         rc;

    if (me == NULL || clientid == NULL) {
        return LMQTT_ERR_PARAM;
    }
    if (strlen(clientid) > LMQTT_CLIENTID_MAX) {
        return LMQTT_ERR_OVERFLOW;
    }

    if (rc_out != NULL) {
        *rc_out = LMQTT_CONN_ACCEPTED;
    }

    if (username != NULL) {
        rc = snprintf(cmd, sizeof(cmd), "AT+LMQTTCONN=%u,\"%s\",\"%s\",\"%s\"",
                      (unsigned)me->tcid, clientid, username,
                      (password != NULL) ? password : "");
    } else {
        rc = snprintf(cmd, sizeof(cmd), "AT+LMQTTCONN=%u,\"%s\"",
                      (unsigned)me->tcid, clientid);
    }
    if (rc < 0 || rc >= (int)sizeof(cmd)) {
        return LMQTT_ERR_OVERFLOW;
    }

    rc = lmqtt_cmd_exec(me, LMQTT_CMD_CONN, 0,
                        LMQTT_TMO_ACK, LMQTT_TMO_CONN, &out, cmd);
    if (rc != LMQTT_OK) {
        return rc;
    }

    if (rc_out != NULL) {
        *rc_out = (lmqtt_conn_rc_t)out.extra;
    }

    /* result: 0=已收到服务器 ACK，1=重传（实测消息已到达），2=发送失败 */
    if (out.result != 0 && out.result != 1) {
        me->connected = false;
        return LMQTT_ERR_RESULT;
    }
    /* ret_code 非 0 表示服务器拒绝（4=用户名密码错误，5=未授权…） */
    if (out.extra != 0) {
        me->connected = false;
        return LMQTT_ERR_RESULT;
    }

    me->connected = true;
    return LMQTT_OK;
}
