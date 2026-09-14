/*
 * lmqtt_core.c - AT 引擎实现
 *
 * 职责：行组帧、URC 分发、命令-响应配对、下行投递。
 * 本文件不掺任何 MQTT 业务语义，只处理 AT 层。
 */
#include "lmqtt_internal.h"

#include <stdio.h>      /* sscanf */
#include <string.h>

/* ------------------------------------------------------------------ */
/* 辅助 */

int32_t lmqtt_cmd_send(lmqtt_t *me, const void *buf, size_t len)
{
    if (me == NULL || buf == NULL) {
        return LMQTT_ERR_PARAM;
    }
    /* 写入的独占性由命令事务的 mutex 保证，而非单次 write 本身 */
    if (me->port->write(buf, len) != (int32_t)len) {
        return LMQTT_ERR_IO;
    }
    return LMQTT_OK;
}

int32_t lmqtt_cmd_send_str(lmqtt_t *me, const char *s)
{
    if (s == NULL) {
        return LMQTT_ERR_PARAM;
    }
    return lmqtt_cmd_send(me, s, strlen(s));
}

static void lmqtt_sem_give(lmqtt_t *me)
{
    if (me->sem != NULL) {
        me->port->sem_give(me->sem);
    }
}

static void lmqtt_sem_reset(lmqtt_t *me)
{
    if (me->sem != NULL) {
        me->port->sem_reset(me->sem);
    }
}

/* ------------------------------------------------------------------ */
/* 下行投递 */

void lmqtt_downlink_put(lmqtt_t *me, const char *data, size_t len)
{
    uint8_t slot;

    if (me->down_ready || len >= LMQTT_DOWN_MAX) {
        me->down_drops++;
        LMQTT_LOG(LMQTT_LOG_WARN, "downlink dropped: busy=%d len=%u drops=%u",
                  (int)me->down_ready, (unsigned)len, (unsigned)me->down_drops);
        return;
    }

    slot = (uint8_t)(me->down_idx ^ 1u);    /* 写另一块，别碰消费者手里那块 */
    memcpy(me->down[slot], data, len);
    me->down[slot][len] = '\0';
    me->down_idx   = slot;
    me->down_ready = true;
}

const char *lmqtt_take_downlink(lmqtt_t *me)
{
    uint8_t slot;

    if (me == NULL || !me->down_ready) {
        return NULL;
    }

    /* 先取槽号再清标志：清标志前 down_ready 仍为真，生产者不会写入 */
    slot = me->down_idx;
    me->down_ready = false;
    return me->down[slot];
}

uint32_t lmqtt_downlink_drops(const lmqtt_t *me)
{
    return (me != NULL) ? me->down_drops : 0U;
}

/* ------------------------------------------------------------------ */
/* STATS 分发 */

void lmqtt_stats_dispatch(lmqtt_t *me, lmqtt_stats_t stat, int32_t extend)
{
    /* 只有 STATS=0 表示链路正常；其余一律视为已断开，交由宿主重连 */
    me->connected = (stat == LMQTT_STATS_OK);

    if (me->stats_cb != NULL) {
        me->stats_cb(me, stat, extend, me->user);
    }
}

/* ------------------------------------------------------------------ */
/* URC 解析 */

/*
 * +LMQTTURC: RECV,<tcpconnectID>,<msgID>,"<topic>",<payload>   （直吐模式）
 * +LMQTTURC: RECV,<tcpconnectID>,<msgID>                        （缓存模式）
 *
 * payload 为行尾剩余全部（可能含逗号），只能用引号定位、不能用逗号切分。
 */
static void lmqtt_urc_recv(lmqtt_t *me, const char *data)
{
    const char *p = data;
    int k;

    /* 跳过 RECV,<tcpconnectID>,<msgID> 共三段。
       跳 4 段会连 topic 一起跳过、指针落到 payload 上，导致 topic/payload 全部错位。 */
    for (k = 0; k < 3; k++) {
        const char *cut = strchr(p, ',');
        if (cut == NULL) {
            return;                 /* 缓存模式：无 topic/payload，交给 LMQTTREAD 读 */
        }
        p = cut + 1;
        while (*p == ' ' || *p == '"') {
            p++;
        }
    }

    /* 定位 topic 的结尾引号，p 随之指向 payload */
    while (*p != '\0' && *p != '"') {
        p++;
    }
    if (*p != '"') {
        return;
    }
    p++;                            /* 跳过 topic 的结尾引号 */
    if (*p == ',') {
        p++;
        while (*p == ' ') {
            p++;                    /* 只跳过分隔空白 */
        }
    } else if (*p == '"') {
        p++;
    }

    /* payload 的取法：
       实测模组投递的是**不带引号**的原文（JSON 也是原样），所以默认逐字节
       原样投递。逐个剥 ' '/'"' 是错的 —— payload 首字符恰好是引号或空格时
       会被静默改数据（旧实现会把 "hello" 变成 hello"）。
       仅当整段确实被一对引号包住（"..."）时才剥这一对，以兼容该形式。 */
    {
        size_t plen = strlen(p);

        if (plen >= 2 && p[0] == '"' && p[plen - 1] == '"') {
            p++;
            plen -= 2;
        }

        /* 只有 topic、没有 payload：不投递，否则消费侧会收到一条空串下行 */
        if (plen == 0) {
            return;
        }

        lmqtt_downlink_put(me, p, plen);
    }
}

/*
 * +LMQTTURC: STATS,<tcpconnectID>,<stats>[,<extend>]
 */
static void lmqtt_urc_stats(lmqtt_t *me, const char *data)
{
    int stat = 0, ext = 0;

    if (sscanf(data, "STATS,%*u,%d,%d", &stat, &ext) < 1) {
        return;
    }

    LMQTT_LOG(LMQTT_LOG_WARN, "STATS stat=%d ext=%d", stat, ext);
    lmqtt_stats_dispatch(me, (lmqtt_stats_t)stat, ext);
}

/* ------------------------------------------------------------------ */
/* 结果 URC 匹配 */

typedef struct lmqtt_urc_entry {
    const char       *prefix;
    lmqtt_cmd_kind_t  kind;
    bool              has_msgid;    /* 格式为 <id>,<msgID>,<result>[,<extend>] */
} lmqtt_urc_entry_t;

static const lmqtt_urc_entry_t s_result_urcs[] = {
    { "+LMQTTOPEN: ",     LMQTT_CMD_OPEN,     false },
    { "+LMQTTCLOSE: ",    LMQTT_CMD_CLOSE,    false },
    { "+LMQTTCONN: ",     LMQTT_CMD_CONN,     false },
    { "+LMQTTDISC: ",     LMQTT_CMD_DISC,     false },
    { "+LMQTTSUBUNSUB: ", LMQTT_CMD_SUBUNSUB, true  },
    /* 手册示例中退订的 URC 拼写为 +LMQTTUNSUNSUB（少一个 B），一并兼容 */
    { "+LMQTTUNSUNSUB: ", LMQTT_CMD_SUBUNSUB, true  },
    { "+LMQTTPUB: ",      LMQTT_CMD_PUB,      true  },
};

/* 返回 true 表示该行已被本引擎消费 */
static bool lmqtt_match_result_urc(lmqtt_t *me, const char *line, size_t len)
{
    size_t i;

    for (i = 0; i < sizeof(s_result_urcs) / sizeof(s_result_urcs[0]); i++) {
        const lmqtt_urc_entry_t *e = &s_result_urcs[i];
        size_t plen = strlen(e->prefix);
        const char *data;
        int result = -1, extra = 0;

        if (len < plen || strncmp(line, e->prefix, plen) != 0) {
            continue;
        }
        data = line + plen;

        if (e->has_msgid) {
            unsigned mid = 0;

            if (sscanf(data, "%*u,%u,%d,%d", &mid, &result, &extra) < 2) {
                return true;        /* 格式不符，消费掉避免误判 */
            }
            /* 迟到/串扰的 URC 不得当作当前命令的结果 */
            if (me->cmd.kind == e->kind && me->cmd.msgid != 0 &&
                mid != (unsigned)me->cmd.msgid) {
                LMQTT_LOG(LMQTT_LOG_DEBUG, "stale %s msgid=%u (expect %u)",
                          e->prefix, mid, (unsigned)me->cmd.msgid);
                return true;
            }
        } else {
            if (sscanf(data, "%*u,%d,%d", &result, &extra) < 1) {
                return true;
            }
        }

        if (me->cmd.kind == e->kind && me->cmd.kind != LMQTT_CMD_NONE) {
            me->cmd.result = result;
            me->cmd.extra  = extra;
            me->cmd.got    = true;
            lmqtt_sem_give(me);
        }

        return true;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* 行分发 */

static void lmqtt_line_dispatch(lmqtt_t *me, const char *line, size_t len)
{
    /* 1. URC（+LMQTT 开头）优先——它们不参与命令-响应配对 */
    if (len > 7 && strncmp(line, "+LMQTT", 6) == 0) {
        const char *data = NULL;

        if (len > 11 && strncmp(line, "+LMQTTURC: ", 11) == 0) {
            data = line + 11;
            if (strncmp(data, "RECV", 4) == 0) {
                lmqtt_urc_recv(me, data);
                return;
            }
            if (strncmp(data, "STATS", 5) == 0) {
                lmqtt_urc_stats(me, data);
                return;
            }
        }

        if (lmqtt_match_result_urc(me, line, len)) {
            return;
        }

        LMQTT_LOG(LMQTT_LOG_WARN, "unmatched urc: %s", line);
        return;
    }

    /* 2. 命令响应终结符 */
    if (len == 2 && memcmp(line, "OK", 2) == 0) {
        me->cmd.acked = true;
        lmqtt_sem_give(me);
        return;
    }
    if (strncmp(line, "ERROR", 5) == 0 || strncmp(line, "+CME ERROR", 10) == 0) {
        me->cmd.rejected = true;
        lmqtt_sem_give(me);
        return;
    }

    LMQTT_LOG(LMQTT_LOG_DEBUG, "rx: %s", line);
}

/* ------------------------------------------------------------------ */
/* 实例生命周期 */

int32_t lmqtt_init(lmqtt_t *me, const lmqtt_port_t *port)
{
    if (me == NULL || port == NULL || port->write == NULL ||
        port->sem_create == NULL || port->sem_take == NULL ||
        port->sem_give == NULL || port->sem_reset == NULL) {
        return LMQTT_ERR_PARAM;
    }

    memset(me, 0, sizeof(*me));
    me->port = port;
    me->tcid = LMQTT_TCID_DEFAULT;

    if (port->mutex_create != NULL) {
        me->mutex = port->mutex_create();
        if (me->mutex == NULL) {
            return LMQTT_ERR_NOMEM;
        }
    }

    me->sem = port->sem_create();
    if (me->sem == NULL) {
        if (me->mutex != NULL && port->mutex_destroy != NULL) {
            port->mutex_destroy(me->mutex);
        }
        me->mutex = NULL;
        return LMQTT_ERR_NOMEM;
    }

    return LMQTT_OK;
}

void lmqtt_deinit(lmqtt_t *me)
{
    if (me == NULL || me->port == NULL) {
        return;
    }

    if (me->sem != NULL && me->port->sem_destroy != NULL) {
        me->port->sem_destroy(me->sem);
    }
    if (me->mutex != NULL && me->port->mutex_destroy != NULL) {
        me->port->mutex_destroy(me->mutex);
    }

    me->sem = NULL;
    me->mutex = NULL;
}

/* ------------------------------------------------------------------ */
/* 接收 */

int32_t lmqtt_rx_feed(lmqtt_t *me, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t i;

    if (me == NULL || buf == NULL) {
        return LMQTT_ERR_PARAM;
    }

    for (i = 0; i < len; i++) {
        char c = (char)p[i];

        if (c == '\n') {
            if (me->line_over) {
                /* 超长行整行丢弃：截断后的 URC 只会被误解析 */
                LMQTT_LOG(LMQTT_LOG_WARN, "line >%u B dropped",
                          (unsigned)(LMQTT_LINE_MAX - 1));
            } else if (me->line_len > 0) {
                me->line[me->line_len] = '\0';
                lmqtt_line_dispatch(me, me->line, me->line_len);
            }
            me->line_len  = 0;
            me->line_over = false;
        } else if (c != '\r') {
            if (me->line_len < LMQTT_LINE_MAX - 1) {
                me->line[me->line_len++] = c;
            } else {
                me->line_over = true;
            }
        }
    }

    return LMQTT_OK;
}

/* ------------------------------------------------------------------ */
/* 状态 */

bool lmqtt_is_connected(const lmqtt_t *me)
{
    return (me != NULL) && me->connected;
}

void lmqtt_set_stats_cb(lmqtt_t *me, lmqtt_stats_cb_t cb, void *user)
{
    if (me == NULL) {
        return;
    }
    me->stats_cb = cb;
    me->user     = user;
}

/* ------------------------------------------------------------------ */
/* 命令执行 */

int32_t lmqtt_cmd_begin(lmqtt_t *me, lmqtt_cmd_kind_t kind, uint16_t msgid)
{
    if (me == NULL) {
        return LMQTT_ERR_PARAM;
    }

    if (me->mutex != NULL) {
        me->port->mutex_lock(me->mutex);
    }

    me->cmd.kind     = (uint8_t)kind;
    me->cmd.msgid    = msgid;
    me->cmd.result   = -1;
    me->cmd.extra    = 0;
    me->cmd.got      = false;
    me->cmd.acked    = false;
    me->cmd.rejected = false;

    lmqtt_sem_reset(me);
    return LMQTT_OK;
}

int32_t lmqtt_cmd_abort(lmqtt_t *me)
{
    if (me == NULL) {
        return LMQTT_ERR_PARAM;
    }

    me->cmd.kind = LMQTT_CMD_NONE;
    if (me->mutex != NULL) {
        me->port->mutex_unlock(me->mutex);
    }
    return LMQTT_OK;
}

int32_t lmqtt_cmd_finish(lmqtt_t *me, uint32_t ack_tmo, uint32_t urc_tmo,
                         lmqtt_cmd_out_t *out)
{
    int32_t rc;
    lmqtt_cmd_kind_t kind;

    if (me == NULL) {
        return LMQTT_ERR_PARAM;
    }

    kind = (lmqtt_cmd_kind_t)me->cmd.kind;

    /* 第一步：等命令被接受。
       注意 `&& !me->cmd.got`：结果 URC 可能先于 OK 到达并把信号量消耗掉
       （cmd.got 已置位）。只看 sem_take 的返回值就报超时，会把已经到手的
       成功判成失败 —— 这是个窗口极小但确实存在的假超时。 */
    if (me->port->sem_take(me->sem, ack_tmo) != 0 && !me->cmd.got) {
        LMQTT_LOG(LMQTT_LOG_WARN, "ack timeout");
        rc = LMQTT_ERR_TIMEOUT;
        goto out;
    }
    if (me->cmd.rejected) {
        LMQTT_LOG(LMQTT_LOG_WARN, "command rejected");
        rc = LMQTT_ERR_AT;
        goto out;
    }

    if (kind == LMQTT_CMD_NONE) {
        rc = LMQTT_OK;
        goto out;
    }

    /* 第二步：等结果 URC。
       实测 CONN 后无静默期，URC 可能先于/伴随 OK 到达，此时 cmd.got 已置位。 */
    if (!me->cmd.got) {
        lmqtt_sem_reset(me);
        if (me->port->sem_take(me->sem, urc_tmo) != 0 && !me->cmd.got) {
            LMQTT_LOG(LMQTT_LOG_WARN, "urc timeout");
            rc = LMQTT_ERR_TIMEOUT;
            goto out;
        }
    }

    if (out != NULL) {
        out->result = me->cmd.result;
        out->extra  = me->cmd.extra;
    }
    rc = LMQTT_OK;

out:
    lmqtt_cmd_abort(me);
    return rc;
}

int32_t lmqtt_cmd_exec(lmqtt_t *me, lmqtt_cmd_kind_t kind, uint16_t msgid,
                       uint32_t ack_tmo, uint32_t urc_tmo,
                       lmqtt_cmd_out_t *out, const char *cmd)
{
    int32_t rc;

    if (me == NULL || cmd == NULL) {
        return LMQTT_ERR_PARAM;
    }

    rc = lmqtt_cmd_begin(me, kind, msgid);
    if (rc != LMQTT_OK) {
        return rc;
    }

    rc = lmqtt_cmd_send_str(me, cmd);
    if (rc == LMQTT_OK) {
        rc = lmqtt_cmd_send(me, "\r\n", 2);
    }
    if (rc != LMQTT_OK) {
        lmqtt_cmd_abort(me);
        return rc;
    }

    return lmqtt_cmd_finish(me, ack_tmo, urc_tmo, out);
}
