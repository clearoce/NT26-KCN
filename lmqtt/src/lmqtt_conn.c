/*
 * lmqtt_conn.c - AT+LMQTTCONN
 */
#include "lmqtt_conn.h"
#include "lmqtt_internal.h"

#include <stdio.h>
#include <string.h>

int32_t lmqtt_conn(lmqtt_t *self, const char *imei,
                   const char *username, const char *password,
                   lmqtt_conn_rc_t *rc_out)
{
    char            cmd[LMQTT_LINE_MAX];
    lmqtt_cmd_out_t out = { 0 };
    int32_t         rc;

    if (self == NULL || imei == NULL) {
        return LMQTT_ERR_PARAM;
    }
    if (strlen(imei) > LMQTT_CLIENTID_MAX) {
        return LMQTT_ERR_OVERFLOW;
    }

    if (rc_out != NULL) {
        *rc_out = LMQTT_CONN_ACCEPTED;
    }

    if (username != NULL) {
        rc = snprintf(cmd, sizeof(cmd), "AT+LMQTTCONN=%u,\"%s\",\"%s\",\"%s\"",
                      (unsigned)self->tcid, imei, username,
                      (password != NULL) ? password : "");
    } else {
        rc = snprintf(cmd, sizeof(cmd), "AT+LMQTTCONN=%u,\"%s\"",
                      (unsigned)self->tcid, imei);
    }
    if (rc < 0 || rc >= (int)sizeof(cmd)) {
        return LMQTT_ERR_OVERFLOW;
    }

    rc = lmqtt_cmd_exec(self, LMQTT_CMD_CONN, 0,
                        LMQTT_TMO_ACK, LMQTT_TMO_CONN, &out, cmd);
    if (rc != LMQTT_OK) {
        return rc;
    }

    if (rc_out != NULL) {
        *rc_out = (lmqtt_conn_rc_t)out.extra;
    }

    /* result: 0=已收到服务器 ACK，1=数据包重传（手册；实测此时消息仍会到达），2=发送失败 */
    if (out.result != 0 && out.result != 1) {
        self->connected = false;
        return LMQTT_ERR_RESULT;
    }
    /* ret_code 非 0 表示服务器拒绝（4=用户名密码错误，5=未授权…） */
    if (out.extra != 0) {
        self->connected = false;
        return LMQTT_ERR_RESULT;
    }

    self->connected = true;
    return LMQTT_OK;
}
