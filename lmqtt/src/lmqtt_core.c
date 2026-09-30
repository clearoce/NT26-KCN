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

int32_t lmqtt_cmd_send(lmqtt_t *self, const void *buf, size_t len)
{
    if (self == NULL || buf == NULL) {
        return LMQTT_ERR_PARAM;
    }
    /* 写入的独占性由命令事务的 mutex 保证，而非单次 write 本身 */
    if (self->port->write(buf, len) != (int32_t)len) {
        return LMQTT_ERR_IO;
    }
    return LMQTT_OK;
}

int32_t lmqtt_cmd_send_str(lmqtt_t *self, const char *s)
{
    if (s == NULL) {
        return LMQTT_ERR_PARAM;
    }
    return lmqtt_cmd_send(self, s, strlen(s));
}

static void lmqtt_sem_give(lmqtt_t *self)
{
    if (self->sem != NULL) {
        self->port->sem_give(self->sem);
    }
}

static void lmqtt_sem_reset(lmqtt_t *self)
{
    if (self->sem != NULL) {
        self->port->sem_reset(self->sem);
    }
}

/* ------------------------------------------------------------------ */
/* 下行投递 */

void lmqtt_downlink_put(lmqtt_t *self, const char *data, size_t len)
{
    uint8_t slot;

    if (self->down_ready || len >= LMQTT_DOWN_MAX) {
        self->down_drops++;
        LMQTT_LOG(LMQTT_LOG_WARN, "downlink dropped: busy=%d len=%u drops=%u",
                  (int)self->down_ready, (unsigned)len, (unsigned)self->down_drops);
        return;
    }

    slot = (uint8_t)(self->down_idx ^ 1u);    /* 写另一块，别碰消费者手里那块 */
    memcpy(self->down[slot], data, len);
    self->down[slot][len] = '\0';
    self->down_idx   = slot;
    self->down_ready = true;
}

const char *lmqtt_take_downlink(lmqtt_t *self)
{
    uint8_t slot;

    if (self == NULL || !self->down_ready) {
        return NULL;
    }

    /* 先取槽号再清标志：清标志前 down_ready 仍为真，生产者不会写入 */
    slot = self->down_idx;
    self->down_ready = false;
    return self->down[slot];
}

uint32_t lmqtt_downlink_drops(const lmqtt_t *self)
{
    return (self != NULL) ? self->down_drops : 0U;
}

uint8_t lmqtt_last_cmd_stage(const lmqtt_t *self)
{
    return (self != NULL) ? (uint8_t)self->cmd.stage : (uint8_t)LMQTT_STAGE_NONE;
}

int32_t lmqtt_last_cmd_result(const lmqtt_t *self)
{
    return (self != NULL) ? self->cmd.result : -1;
}

int32_t lmqtt_last_cmd_extra(const lmqtt_t *self)
{
    return (self != NULL) ? self->cmd.extra : 0;
}

void lmqtt_get_err_counters(const lmqtt_t *self, lmqtt_err_counters_t *out)
{
    if (out == NULL) {
        return;
    }
    if (self == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }

    out->down_drops     = self->down_drops;
    out->line_drops     = self->line_drops;
    out->unmatched_urcs = self->unmatched_urcs;
    out->bad_pub_acks   = self->bad_pub_acks;

    out->stray_oks      = self->stray_oks;
    out->stray_errs     = self->stray_errs;
    out->extra_acks     = self->extra_acks;
    out->extra_errs     = self->extra_errs;
    out->early_acks     = self->early_acks;
    out->early_errs     = self->early_errs;
    out->stale_urcs     = self->stale_urcs;
    out->rx_other       = self->rx_other;
}

/* ------------------------------------------------------------------ */
/* STATS 分发 */

void lmqtt_stats_dispatch(lmqtt_t *self, lmqtt_stats_t stat, int32_t extend)
{
    /* 只有 STATS=0 表示链路正常；其余一律视为已断开，交由宿主重连 */
    self->connected = (stat == LMQTT_STATS_OK);

    if (self->stats_cb != NULL) {
        self->stats_cb(self, stat, extend, self->user);
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
static void lmqtt_urc_recv(lmqtt_t *self, const char *data)
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

        lmqtt_downlink_put(self, p, plen);
    }
}

/*
 * +LMQTTURC: STATS,<tcpconnectID>,<stats>[,<extend>]
 */
static void lmqtt_urc_stats(lmqtt_t *self, const char *data)
{
    int stat = 0, ext = 0;

    if (sscanf(data, "STATS,%*u,%d,%d", &stat, &ext) < 1) {
        return;
    }

    LMQTT_LOG(LMQTT_LOG_WARN, "STATS stat=%d ext=%d", stat, ext);
    lmqtt_stats_dispatch(self, (lmqtt_stats_t)stat, ext);
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
static bool lmqtt_match_result_urc(lmqtt_t *self, const char *line, size_t len)
{
    size_t i;
    /* 配对上下文整字快照一次，本行的过滤与放行都只看这一份：分别读
       kind/msgid 会被 cmd_begin 的发布切成"新 kind + 旧 msgid"，从而把
       上一条命令的迟到 URC 放行成当前命令的结果。 */
    uint32_t         pend  = self->cmd.pending;
    lmqtt_cmd_kind_t ckind = LMQTT_PEND_KIND(pend);
    uint16_t         cmid  = LMQTT_PEND_MSGID(pend);

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
            if (ckind == e->kind && cmid != 0 && mid != (unsigned)cmid) {
                self->stale_urcs++;
                LMQTT_LOG(LMQTT_LOG_DEBUG, "stale %s msgid=%u (expect %u)",
                          e->prefix, mid, (unsigned)cmid);
                return true;
            }
        } else {
            if (sscanf(data, "%*u,%d,%d", &result, &extra) < 1) {
                return true;
            }
        }

        /* 写回前复核配对字没被换掉（abort 或下一条命令的 begin）：单次 32 位
           比较，几乎零成本。解析期间命令侧若已放弃本条命令，就不要再往它的
           结果槽里写。 */
        if (ckind == e->kind && ckind != LMQTT_CMD_NONE &&
            self->cmd.pending == pend) {
            self->cmd.result    = result;
            self->cmd.extra     = extra;
            self->cmd.delivered = pend;       /* 给结果署名，命令侧据此认领 */
            self->cmd.got       = true;       /* 最后置位：前面的字段先可见 */
            lmqtt_sem_give(self);
        }

        return true;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* 行分发 */

static void lmqtt_line_dispatch(lmqtt_t *self, const char *line, size_t len)
{
    /* 1. URC（+LMQTT 开头）优先——它们不参与命令-响应配对 */
    if (len > 7 && strncmp(line, "+LMQTT", 6) == 0) {
        const char *data = NULL;

        if (len > 11 && strncmp(line, "+LMQTTURC: ", 11) == 0) {
            data = line + 11;
            if (strncmp(data, "RECV", 4) == 0) {
                lmqtt_urc_recv(self, data);
                return;
            }
            if (strncmp(data, "STATS", 5) == 0) {
                lmqtt_urc_stats(self, data);
                return;
            }
        }

        if (lmqtt_match_result_urc(self, line, len)) {
            return;
        }

        self->unmatched_urcs++;
        LMQTT_LOG(LMQTT_LOG_WARN, "unmatched urc: %s", line);
        return;
    }

    /* 2. 命令响应终结符。
       AT 的 OK/ERROR **不带命令标识** ⇒ 这里做不到"认领"：只要一条 OK 到达，
       它就被算给当时在途的命令（cmd_finish 只做 sem_take，不读归属）。
       本分支额外把"窗外到达的"与"窗内多来的"分别记进计数器（见
       lmqtt_cmd_ctx_t.busy 与 lmqtt_err_counters 的 stray_ 与 extra_ 两组计数），
       供宿主判断这类串扰的真实频率。
       **置位与 sem_give 的行为与加计数之前完全一致。** */
    if (len == 2 && memcmp(line, "OK", 2) == 0) {
        /* 栅栏（见 lmqtt_cmd_ctx_t.written）：命令**还没写完**时到达的 OK，
           模组那时还没收到命令 ⇒ **必非本命令的受理**。只计数，不置位、不放行
           信号量 —— 否则一条外来的 OK 就能把一条根本没被受理的命令判成成功。
           ⚠️ 开窗条件必须是"命令已真正写出"：当前 IOT_UART_TX_INT = 0（轮询发送），
           send 返回即末字节进寄存器，故 cmd_finish 入口置 written 成立；
           **将来若启用 TX_INT**（send 变成"塞流即返回"），这里必须改判"TX 流空/TC"，
           否则窗口开早、真 OK 反被栅栏挡掉。 */
        if (self->cmd.busy && !self->cmd.written) {
            self->early_acks++;
            return;
        }
        if (self->cmd.busy) {
            if (self->cmd.acked) {
                self->extra_acks++;          /* 本窗第 2 条及以后 */
            }
        } else {
            self->stray_oks++;
        }
        self->cmd.acked = true;
        lmqtt_sem_give(self);
        return;
    }
    if (strncmp(line, "ERROR", 5) == 0 || strncmp(line, "+CME ERROR", 10) == 0) {
        if (self->cmd.busy && !self->cmd.written) {
            self->early_errs++;              /* 同上：写出去之前来的 ERROR 也不认 */
            return;
        }
        if (self->cmd.busy) {
            if (self->cmd.rejected) {
                self->extra_errs++;
            }
        } else {
            self->stray_errs++;
        }
        self->cmd.rejected = true;
        lmqtt_sem_give(self);
        return;
    }

    /* 既不是 +LMQTT URC、也不是终结符 —— 宿主那边 durian 库的 AT 响应行
       （+CSQ / +CEREG / +CGPADDR / +CCLK …）与模组启动横幅都落这里。
       只计数：它**不是故障**，不要接进任何错误码；它的用途是让"库到底看见了多少
       无关行"能与命令窗口对齐。 */
    self->rx_other++;
    LMQTT_LOG(LMQTT_LOG_DEBUG, "rx: %s", line);
}

/* ------------------------------------------------------------------ */
/* 实例生命周期 */

int32_t lmqtt_init(lmqtt_t *self, const lmqtt_port_t *port)
{
    if (self == NULL || port == NULL || port->write == NULL ||
        port->sem_create == NULL || port->sem_take == NULL ||
        port->sem_give == NULL || port->sem_reset == NULL) {
        return LMQTT_ERR_PARAM;
    }

    memset(self, 0, sizeof(*self));
    self->port = port;
    self->tcid = LMQTT_TCID_DEFAULT;

    if (port->mutex_create != NULL) {
        self->mutex = port->mutex_create();
        if (self->mutex == NULL) {
            return LMQTT_ERR_NOMEM;
        }
    }

    self->sem = port->sem_create();
    if (self->sem == NULL) {
        if (self->mutex != NULL && port->mutex_destroy != NULL) {
            port->mutex_destroy(self->mutex);
        }
        self->mutex = NULL;
        return LMQTT_ERR_NOMEM;
    }

    return LMQTT_OK;
}

void lmqtt_deinit(lmqtt_t *self)
{
    if (self == NULL || self->port == NULL) {
        return;
    }

    if (self->sem != NULL && self->port->sem_destroy != NULL) {
        self->port->sem_destroy(self->sem);
    }
    if (self->mutex != NULL && self->port->mutex_destroy != NULL) {
        self->port->mutex_destroy(self->mutex);
    }

    self->sem = NULL;
    self->mutex = NULL;
}

/* ------------------------------------------------------------------ */
/* 接收 */

int32_t lmqtt_rx_feed(lmqtt_t *self, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t i;

    if (self == NULL || buf == NULL) {
        return LMQTT_ERR_PARAM;
    }

    for (i = 0; i < len; i++) {
        char c = (char)p[i];

        if (c == '\n') {
            if (self->line_over) {
                /* 超长行整行丢弃：截断后的 URC 只会被误解析 */
                self->line_drops++;
                LMQTT_LOG(LMQTT_LOG_WARN, "line >%u B dropped",
                          (unsigned)(LMQTT_LINE_MAX - 1));
            } else if (self->line_len > 0) {
                self->line[self->line_len] = '\0';
                lmqtt_line_dispatch(self, self->line, self->line_len);
            }
            self->line_len  = 0;
            self->line_over = false;
        } else if (c != '\r') {
            if (self->line_len < LMQTT_LINE_MAX - 1) {
                self->line[self->line_len++] = c;
            } else {
                self->line_over = true;
            }
        }
    }

    return LMQTT_OK;
}

/* ------------------------------------------------------------------ */
/* 状态 */

bool lmqtt_is_connected(const lmqtt_t *self)
{
    return (self != NULL) && self->connected;
}

void lmqtt_set_stats_cb(lmqtt_t *self, lmqtt_stats_cb_t cb, void *user)
{
    if (self == NULL) {
        return;
    }
    self->stats_cb = cb;
    self->user     = user;
}

/* ------------------------------------------------------------------ */
/* 命令执行 */

int32_t lmqtt_cmd_begin(lmqtt_t *self, lmqtt_cmd_kind_t kind, uint16_t msgid)
{
    if (self == NULL) {
        return LMQTT_ERR_PARAM;
    }

    if (self->mutex != NULL) {
        self->port->mutex_lock(self->mutex);
    }

    /* 顺序要紧：先复位结果槽，最后才整字发布 (kind, msgid)。
       发布是接收侧的放行条件，必须排在复位之后 —— 否则接收侧可能先看到新
       配对、写入 result/got，紧接着被这里的复位抹掉。该窗口内接收侧匹配到
       的只可能是上一条命令的迟到 URC，复位先行使之不会被写进结果槽。 */
    self->cmd.result    = -1;
    self->cmd.extra     = 0;
    self->cmd.delivered = LMQTT_CMD_PEND(LMQTT_CMD_NONE, 0);
    self->cmd.got       = false;
    self->cmd.acked     = false;
    self->cmd.rejected  = false;
    self->cmd.stage     = LMQTT_STAGE_NONE;   /* 只在这里复位，abort 不动它 */

    /* 开窗必须排在上面那组复位**之后**：若先置 busy，一条在这个缝里到达的 OK
       会把 acked 置真，紧接着被复位抹掉 —— 命令就丢掉了自己的受理。 */
    self->cmd.written   = false;                         /* 命令尚未写出 */
    self->cmd.busy      = true;                          /* 开窗（见 lmqtt_cmd_ctx_t） */
    self->cmd.pending   = LMQTT_CMD_PEND(kind, msgid);   /* 单次对齐 32 位发布 */

    lmqtt_sem_reset(self);
    return LMQTT_OK;
}

int32_t lmqtt_cmd_abort(lmqtt_t *self)
{
    if (self == NULL) {
        return LMQTT_ERR_PARAM;
    }

    /* 先关窗、再清配对字：反过来的话，这两步之间到达的 OK 会被算成"窗内"
       （而窗口其实已经结束），把它计进 extra_* 会虚增串扰。 */
    self->cmd.busy    = false;

    /* 整字清除：kind 归 NONE 的同时把 msgid 一并清零。msgid 残留会让同 kind
       的下一条命令期间，接收侧的快照命中上一条的 msgid，把迟到 URC 当成
       当前命令的结果。 */
    self->cmd.pending = LMQTT_CMD_PEND(LMQTT_CMD_NONE, 0);
    if (self->mutex != NULL) {
        self->port->mutex_unlock(self->mutex);
    }
    return LMQTT_OK;
}

/* 结果是否属于指定配对：不但要有结果（got），结果槽还得署着本命令的配对字。
   只认 got 会被上一条命令的迟到投递骗过 —— 接收侧写回前复核过配对字，但
   复核与写回之间仍可能被抢占，所以命令侧必须自己再认领一次。 */
static bool lmqtt_result_ready(const lmqtt_t *self, uint32_t pend)
{
    return self->cmd.got && self->cmd.delivered == pend;
}

int32_t lmqtt_cmd_finish(lmqtt_t *self, uint32_t ack_tmo, uint32_t urc_tmo,
                         lmqtt_cmd_out_t *out)
{
    int32_t rc;
    uint32_t pend;
    lmqtt_cmd_kind_t kind;

    if (self == NULL) {
        return LMQTT_ERR_PARAM;
    }

    /* 走到本函数说明 cmd_exec 里两次 send 都已返回（发送失败会直接 abort 掉，
       不会到这里）⇒ 现在起到达的 OK 才**有可能**是本命令的。此前到达的算
       early_*（见 lmqtt_cmd_ctx_t.written）。 */
    self->cmd.written = true;

    pend = self->cmd.pending;             /* 本命令的配对字，全程不变 */
    kind = LMQTT_PEND_KIND(pend);

    /* 第一步：等命令被接受。
       注意 `&& !self->cmd.got`：结果 URC 可能先于 OK 到达并把信号量消耗掉
       （cmd.got 已置位）。只看 sem_take 的返回值就报超时，会把已经到手的
       成功判成失败 —— 这是个窗口极小但确实存在的假超时。 */
    if (self->port->sem_take(self->sem, ack_tmo) != 0 &&
        !lmqtt_result_ready(self, pend)) {
        LMQTT_LOG(LMQTT_LOG_WARN, "ack timeout");
        self->cmd.stage = LMQTT_STAGE_ACK_TMO;
        rc = LMQTT_ERR_TIMEOUT;
        goto out;
    }
    if (self->cmd.rejected) {
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
    if (!lmqtt_result_ready(self, pend)) {
        lmqtt_sem_reset(self);
        if (self->port->sem_take(self->sem, urc_tmo) != 0 &&
            !lmqtt_result_ready(self, pend)) {
            LMQTT_LOG(LMQTT_LOG_WARN, "urc timeout");
            self->cmd.stage = LMQTT_STAGE_URC_TMO;
            rc = LMQTT_ERR_TIMEOUT;
            goto out;
        }
    }

    /* 认领结果槽：走到这里 got 必已置位，但那份结果未必是本命令的
       （见 lmqtt_result_ready）。署名不符说明本命令的结果始终没到，
       按超时处理，好过把别人的结果当成自己的成功。 */
    if (!lmqtt_result_ready(self, pend)) {
        LMQTT_LOG(LMQTT_LOG_WARN, "stale result slot");
        self->cmd.stage = LMQTT_STAGE_STALE_SLOT;
        rc = LMQTT_ERR_TIMEOUT;
        goto out;
    }

    if (out != NULL) {
        out->result = self->cmd.result;
        out->extra  = self->cmd.extra;
    }
    rc = LMQTT_OK;

out:
    lmqtt_cmd_abort(self);
    return rc;
}

int32_t lmqtt_cmd_exec(lmqtt_t *self, lmqtt_cmd_kind_t kind, uint16_t msgid,
                       uint32_t ack_tmo, uint32_t urc_tmo,
                       lmqtt_cmd_out_t *out, const char *cmd)
{
    int32_t rc;

    if (self == NULL || cmd == NULL) {
        return LMQTT_ERR_PARAM;
    }

    rc = lmqtt_cmd_begin(self, kind, msgid);
    if (rc != LMQTT_OK) {
        return rc;
    }

    rc = lmqtt_cmd_send_str(self, cmd);
    if (rc == LMQTT_OK) {
        rc = lmqtt_cmd_send(self, "\r\n", 2);
    }
    if (rc != LMQTT_OK) {
        lmqtt_cmd_abort(self);
        return rc;
    }

    return lmqtt_cmd_finish(self, ack_tmo, urc_tmo, out);
}
