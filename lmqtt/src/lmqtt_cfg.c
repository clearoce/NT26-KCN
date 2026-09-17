/*
 * lmqtt_cfg.c - AT+LMQTTCFG 配置
 */
#include "lmqtt_cfg.h"
#include "lmqtt_internal.h"

#include <stdio.h>

/* 配置命令统一只等 OK，无结果 URC */
static int32_t cfg_submit(lmqtt_t *self, const char *cmd)
{
    return lmqtt_cmd_exec(self, LMQTT_CMD_NONE, 0, LMQTT_TMO_CFG, 0, NULL, cmd);
}

int32_t lmqtt_cfg_write(lmqtt_t *self, const char *key, const char *args)
{
    char cmd[LMQTT_LINE_MAX];

    if (self == NULL || key == NULL) {
        return LMQTT_ERR_PARAM;
    }
    if (args == NULL) {
        args = "";
    }

    if (snprintf(cmd, sizeof(cmd), "AT+LMQTTCFG=\"%s\",%u%s",
                 key, (unsigned)self->tcid, args) >= (int)sizeof(cmd)) {
        return LMQTT_ERR_OVERFLOW;
    }

    return cfg_submit(self, cmd);
}

int32_t lmqtt_cfg_cache(lmqtt_t *self, lmqtt_cache_mode_t mode)
{
    char cmd[48];

    if (self == NULL) {
        return LMQTT_ERR_PARAM;
    }
    snprintf(cmd, sizeof(cmd), "AT+LMQTTCFG=\"cache\",%u,%u",
             (unsigned)self->tcid, (unsigned)mode);
    return cfg_submit(self, cmd);
}

int32_t lmqtt_cfg_session(lmqtt_t *self, uint8_t clean_session)
{
    char cmd[48];

    if (self == NULL) {
        return LMQTT_ERR_PARAM;
    }
    snprintf(cmd, sizeof(cmd), "AT+LMQTTCFG=\"session\",%u,%u",
             (unsigned)self->tcid, (unsigned)clean_session);
    return cfg_submit(self, cmd);
}

int32_t lmqtt_cfg_keepalive(lmqtt_t *self, uint16_t seconds)
{
    char cmd[48];

    if (self == NULL) {
        return LMQTT_ERR_PARAM;
    }
    if (seconds > 3600) {
        return LMQTT_ERR_PARAM;             /* 手册范围 0~3600 */
    }
    snprintf(cmd, sizeof(cmd), "AT+LMQTTCFG=\"keepalive\",%u,%u",
             (unsigned)self->tcid, (unsigned)seconds);
    return cfg_submit(self, cmd);
}

int32_t lmqtt_cfg_dataformat(lmqtt_t *self, lmqtt_data_format_t send_fmt,
                             lmqtt_data_format_t recv_fmt)
{
    char cmd[64];

    if (self == NULL) {
        return LMQTT_ERR_PARAM;
    }
    snprintf(cmd, sizeof(cmd), "AT+LMQTTCFG=\"dataformat\",%u,%u,%u",
             (unsigned)self->tcid, (unsigned)send_fmt, (unsigned)recv_fmt);
    return cfg_submit(self, cmd);
}

int32_t lmqtt_cfg_urc(lmqtt_t *self, bool on)
{
    char cmd[48];

    if (self == NULL) {
        return LMQTT_ERR_PARAM;
    }
    snprintf(cmd, sizeof(cmd), "AT+LMQTTCFG=\"urc\",%u,%u",
             (unsigned)self->tcid, (unsigned)(on ? 1 : 0));
    return cfg_submit(self, cmd);
}

int32_t lmqtt_cfg_send_mode(lmqtt_t *self, lmqtt_data_format_t fmt)
{
    char cmd[48];

    if (self == NULL) {
        return LMQTT_ERR_PARAM;
    }
    snprintf(cmd, sizeof(cmd), "AT+LMQTTCFG=\"send/mode\",%u,%u",
             (unsigned)self->tcid, (unsigned)fmt);
    return cfg_submit(self, cmd);
}

int32_t lmqtt_cfg_recv_mode(lmqtt_t *self, lmqtt_data_format_t fmt, bool show_len)
{
    char cmd[64];

    if (self == NULL) {
        return LMQTT_ERR_PARAM;
    }
    snprintf(cmd, sizeof(cmd), "AT+LMQTTCFG=\"recv/mode\",%u,%u,%u",
             (unsigned)self->tcid, (unsigned)fmt, (unsigned)(show_len ? 1 : 0));
    return cfg_submit(self, cmd);
}

int32_t lmqtt_cfg_raimode(lmqtt_t *self, uint8_t mode)
{
    char cmd[48];

    if (self == NULL) {
        return LMQTT_ERR_PARAM;
    }
    snprintf(cmd, sizeof(cmd), "AT+LMQTTCFG=\"raimode\",%u,%u",
             (unsigned)self->tcid, (unsigned)mode);
    return cfg_submit(self, cmd);
}

int32_t lmqtt_cfg_sslenable(lmqtt_t *self, bool enable, uint8_t ssl_ctx_id)
{
    char cmd[64];

    if (self == NULL) {
        return LMQTT_ERR_PARAM;
    }
    snprintf(cmd, sizeof(cmd), "AT+LMQTTCFG=\"sslenable\",%u,%u,%u",
             (unsigned)self->tcid, (unsigned)(enable ? 1 : 0),
             (unsigned)ssl_ctx_id);
    return cfg_submit(self, cmd);
}
