/*
 * test_core.c - AT 引擎的 PC 端单元测试
 *
 * 用 mock 移植层驱动：mock 的 sem_take 在被调用时把「预设的模组响应」
 * 喂进 lmqtt_rx_feed，从而在没有真实串口的情况下复现完整的收发时序。
 *
 * 构建运行：
 *   gcc -I../include -o test_core test_core.c ../src/lmqtt_core.c && ./test_core
 */
#include "lmqtt_core.h"
#include "lmqtt_internal.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* mock 移植层 */

static lmqtt_t      g_ctx;
static char         g_tx[1024];
static size_t       g_tx_len;

/* 每次 sem_take 消费一个 step（模拟模组分次回包） */
static const char  *g_steps[8];
static int          g_step_idx;

static int32_t mock_write(const void *buf, size_t len)
{
    if (g_tx_len + len >= sizeof(g_tx)) {
        return -1;
    }
    memcpy(g_tx + g_tx_len, buf, len);
    g_tx_len += len;
    return (int32_t)len;
}

static void *mock_mutex_create(void) { return (void *)1; }
static void  mock_mutex_lock(void *m) { (void)m; }
static void  mock_mutex_unlock(void *m) { (void)m; }

static void *mock_sem_create(void) { return (void *)1; }

/* 用计数模拟二值信号量：只有引擎调用 give 才算被唤醒。
   若 mock 仅"有预设响应就返回成功"，就测不出 msgID 校验这类
   「引擎选择不 give」的行为。 */
static int g_sem_count;

static int32_t mock_sem_take(void *s, uint32_t timeout_ms)
{
    (void)s;
    (void)timeout_ms;

    if (g_sem_count > 0) {
        g_sem_count--;
        return 0;
    }

    /* 没有待处理信号：喂入一步模拟的模组回包，再看引擎是否被唤醒 */
    if (g_steps[g_step_idx] != NULL) {
        lmqtt_rx_feed(&g_ctx, g_steps[g_step_idx], strlen(g_steps[g_step_idx]));
        g_step_idx++;

        if (g_sem_count > 0) {
            g_sem_count--;
            return 0;
        }
    }

    return 1;                           /* 超时 */
}

static void mock_sem_give(void *s)  { (void)s; g_sem_count++; }
static void mock_sem_reset(void *s) { (void)s; g_sem_count = 0; }

static const lmqtt_port_t g_port = {
    .write        = mock_write,
    .mutex_create = mock_mutex_create,
    .mutex_lock   = mock_mutex_lock,
    .mutex_unlock = mock_mutex_unlock,
    .sem_create   = mock_sem_create,
    .sem_take     = mock_sem_take,
    .sem_give     = mock_sem_give,
    .sem_reset    = mock_sem_reset,
};

/* ------------------------------------------------------------------ */
/* 测试框架 */

static int g_pass, g_fail;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) {                                                          \
            g_pass++;                                                        \
        } else {                                                             \
            g_fail++;                                                        \
            printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__);         \
        }                                                                    \
    } while (0)

static void reset_steps(const char *s0, const char *s1, const char *s2)
{
    g_steps[0] = s0;
    g_steps[1] = s1;
    g_steps[2] = s2;
    g_steps[3] = NULL;
    g_step_idx = 0;
    g_tx_len   = 0;
}

static void setup(void)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_sem_count = 0;
    lmqtt_init(&g_ctx, &g_port);
}

/* ------------------------------------------------------------------ */
/* 用例 */

/* PUB 成功：OK 先到，结果 URC 后到 */
static void test_pub_ok(void)
{
    lmqtt_cmd_out_t out = { 0 };

    printf("test_pub_ok\n");
    setup();
    reset_steps("OK\r\n", "+LMQTTPUB: 0,5,0\r\n", NULL);

    int32_t rc = lmqtt_cmd_exec(&g_ctx, LMQTT_CMD_PUB, 5,
                                LMQTT_TMO_ACK, LMQTT_TMO_PUB, &out,
                                "AT+LMQTTPUB=0,5,1,0,\"t\",4,\"abcd\"");

    CHECK(rc == LMQTT_OK, "命令应成功");
    CHECK(out.result == LMQTT_RES_OK, "result 应为 0");
    CHECK(strstr(g_tx, "AT+LMQTTPUB=0,5,1,0,\"t\",4,\"abcd\"\r\n") != NULL,
          "应写出完整命令并补 CRLF");
}

/* PUB 的 result=1（手册：发送成功了，但响应错误 ACK）——引擎不判定业务语义，原样回填 */
static void test_pub_ack_err(void)
{
    lmqtt_cmd_out_t out = { 0 };

    printf("test_pub_ack_err\n");
    setup();
    reset_steps("OK\r\n", "+LMQTTPUB: 0,7,1,6\r\n", NULL);

    int32_t rc = lmqtt_cmd_exec(&g_ctx, LMQTT_CMD_PUB, 7,
                                LMQTT_TMO_ACK, LMQTT_TMO_PUB, &out, "AT+LMQTTPUB=...");

    CHECK(rc == LMQTT_OK, "命令应成功");
    CHECK(out.result == 1, "result 应为 1（PUB：已发出但 ACK 异常）");
    CHECK(out.extra == 6, "extend 应为 6");
}

/* 迟到的 URC（msgID 不匹配）不得被认作当前命令的结果 */
static void test_stale_msgid_ignored(void)
{
    lmqtt_cmd_out_t out = { 0 };

    printf("test_stale_msgid_ignored\n");
    setup();
    /* 期望 msgid=5，但模组先吐了一条 msgid=99 的旧 URC，之后没有任何新响应 */
    reset_steps("OK\r\n", "+LMQTTPUB: 0,99,0\r\n", NULL);

    int32_t rc = lmqtt_cmd_exec(&g_ctx, LMQTT_CMD_PUB, 5,
                                LMQTT_TMO_ACK, LMQTT_TMO_PUB, &out, "AT+LMQTTPUB=...");

    CHECK(rc == LMQTT_ERR_TIMEOUT, "msgID 不匹配时应超时，而非误判成功");
}

/* URC 先于 OK 到达（实测 CONN 后无静默期） */
static void test_urc_before_ok(void)
{
    lmqtt_cmd_out_t out = { 0 };

    printf("test_urc_before_ok\n");
    setup();
    reset_steps("+LMQTTCONN: 0,0,0\r\nOK\r\n", NULL, NULL);

    int32_t rc = lmqtt_cmd_exec(&g_ctx, LMQTT_CMD_CONN, 0,
                                LMQTT_TMO_ACK, LMQTT_TMO_CONN, &out, "AT+LMQTTCONN=...");

    CHECK(rc == LMQTT_OK, "URC 先到时也应收敛");
    CHECK(out.result == LMQTT_RES_OK, "result 应为 0");
    CHECK(out.extra == 0, "ret_code 应为 0");
}

/* 模组拒绝命令 */
static void test_rejected(void)
{
    printf("test_rejected\n");
    setup();
    reset_steps("ERROR\r\n", NULL, NULL);

    int32_t rc = lmqtt_cmd_exec(&g_ctx, LMQTT_CMD_PUB, 1,
                                LMQTT_TMO_ACK, LMQTT_TMO_PUB, NULL, "AT+LMQTTPUB=...");
    CHECK(rc == LMQTT_ERR_AT, "ERROR 应返回 LMQTT_ERR_AT");

    reset_steps("+CME ERROR: 3\r\n", NULL, NULL);
    rc = lmqtt_cmd_exec(&g_ctx, LMQTT_CMD_PUB, 1,
                        LMQTT_TMO_ACK, LMQTT_TMO_PUB, NULL, "AT+LMQTTPUB=...");
    CHECK(rc == LMQTT_ERR_AT, "+CME ERROR 应返回 LMQTT_ERR_AT");
}

/* 只等 OK 的命令（LMQTTCFG 类） */
static void test_cfg_simple(void)
{
    printf("test_cfg_simple\n");
    setup();
    reset_steps("OK\r\n", NULL, NULL);

    int32_t rc = lmqtt_cmd_exec(&g_ctx, LMQTT_CMD_NONE, 0,
                                LMQTT_TMO_ACK, 0, NULL, "AT+LMQTTCFG=\"cache\",0,0");
    CHECK(rc == LMQTT_OK, "CFG 命令应成功");
}

/* 下行 RECV 解析（直吐模式） */
static void test_recv_downlink(void)
{
    const char *payload;

    printf("test_recv_downlink\n");
    setup();
    lmqtt_rx_feed(&g_ctx,
        "+LMQTTURC: RECV,0,1,\"standard-config/861326070275653/var_write\","
        "{\"control\":1,\"message_id\":\"t1\"}\r\n",
        strlen("+LMQTTURC: RECV,0,1,\"standard-config/861326070275653/var_write\","
               "{\"control\":1,\"message_id\":\"t1\"}\r\n"));

    payload = lmqtt_take_downlink(&g_ctx);
    CHECK(payload != NULL, "应取到下行 payload");
    if (payload != NULL) {
        CHECK(strcmp(payload, "{\"control\":1,\"message_id\":\"t1\"}") == 0,
              "payload 应与原文逐字节一致");
    }

    /* 取走后再次调用应返回 NULL */
    CHECK(lmqtt_take_downlink(&g_ctx) == NULL, "单槽缓冲取走一次后应为空");
}

/* 缓存模式下的 RECV 不携带 payload，不应误产出一条下行 */
static void test_recv_cache_mode(void)
{
    printf("test_recv_cache_mode\n");
    setup();
    lmqtt_rx_feed(&g_ctx, "+LMQTTURC: RECV,0,5\r\n", strlen("+LMQTTURC: RECV,0,5\r\n"));

    CHECK(lmqtt_take_downlink(&g_ctx) == NULL, "缓存模式不应投递 payload");
}

/* STATS 断线通知 */
static int g_stats_calls;
static lmqtt_stats_t g_last_stat;

static void on_stats(lmqtt_t *self, lmqtt_stats_t stat, int32_t ext, void *user)
{
    (void)self; (void)ext; (void)user;
    g_stats_calls++;
    g_last_stat = stat;
}

static void test_stats(void)
{
    printf("test_stats\n");
    setup();
    g_stats_calls = 0;
    lmqtt_set_stats_cb(&g_ctx, on_stats, NULL);

    g_ctx.connected = true;
    lmqtt_rx_feed(&g_ctx, "+LMQTTURC: STATS,0,1\r\n", strlen("+LMQTTURC: STATS,0,1\r\n"));

    CHECK(g_stats_calls == 1, "STATS 应触发回调一次");
    CHECK(g_last_stat == LMQTT_STATS_PEER_RESET, "stat 应为 1");
    CHECK(!lmqtt_is_connected(&g_ctx), "收到断线 STATS 后应转为未连接");
}

/* 超长行整行丢弃，不得被截断后误解析 */
static void test_overflow_line_dropped(void)
{
    char big[LMQTT_LINE_MAX + 200];

    printf("test_overflow_line_dropped\n");
    setup();

    memset(big, 'A', sizeof(big) - 3);
    big[sizeof(big) - 3] = '\r';
    big[sizeof(big) - 2] = '\n';
    big[sizeof(big) - 1] = '\0';

    lmqtt_rx_feed(&g_ctx, big, strlen(big));
    /* 溢出行被丢弃后，引擎应恢复正常，后续正常行仍能被处理 */
    lmqtt_rx_feed(&g_ctx,
                  "+LMQTTURC: RECV,0,1,\"t\",payload\r\n",
                  strlen("+LMQTTURC: RECV,0,1,\"t\",payload\r\n"));

    const char *p = lmqtt_take_downlink(&g_ctx);
    CHECK(p != NULL && strcmp(p, "payload") == 0,
          "超长行丢弃后，下一行应仍能正常解析");
}

/* 下行缓冲被占用时计数丢弃，不覆盖未取走的数据 */
static void test_downlink_busy_drop(void)
{
    printf("test_downlink_busy_drop\n");
    setup();

    lmqtt_rx_feed(&g_ctx, "+LMQTTURC: RECV,0,1,\"t\",first\r\n",
                  strlen("+LMQTTURC: RECV,0,1,\"t\",first\r\n"));
    lmqtt_rx_feed(&g_ctx, "+LMQTTURC: RECV,0,2,\"t\",second\r\n",
                  strlen("+LMQTTURC: RECV,0,2,\"t\",second\r\n"));

    const char *p = lmqtt_take_downlink(&g_ctx);
    CHECK(p != NULL && strcmp(p, "first") == 0, "未取走的数据不应被覆盖");
    CHECK(lmqtt_downlink_drops(&g_ctx) == 1, "覆盖尝试应计入 drops");
}

/* take 之后 RX 再写一条，不得动到调用方手里那块缓冲。
   单缓冲实现会在这里露馅：take 一清 down_ready，RX 就就地覆写。 */
static void test_take_survives_next_put(void)
{
    const char *p;

    printf("test_take_survives_next_put\n");
    setup();

    lmqtt_rx_feed(&g_ctx, "+LMQTTURC: RECV,0,1,\"t\",FIRST\r\n",
                  strlen("+LMQTTURC: RECV,0,1,\"t\",FIRST\r\n"));
    p = lmqtt_take_downlink(&g_ctx);
    CHECK(p != NULL && strcmp(p, "FIRST") == 0, "第一条应取到");

    /* 调用方还在用 p 期间，RX 侧来了第二条 */
    lmqtt_rx_feed(&g_ctx, "+LMQTTURC: RECV,0,2,\"t\",SECOND\r\n",
                  strlen("+LMQTTURC: RECV,0,2,\"t\",SECOND\r\n"));

    CHECK(p != NULL && strcmp(p, "FIRST") == 0,
          "取出后 RX 再写入不得覆写调用方手里的数据");

    const char *q = lmqtt_take_downlink(&g_ctx);
    CHECK(q != NULL && strcmp(q, "SECOND") == 0, "第二条应取到");
}

/* payload 逐字节原样投递；只有"整段被一对引号包住"时才剥这一对。
   旧实现无条件剥掉首字符，会把 "a",1 变成 a",1 —— 静默改数据。 */
static void test_recv_payload_verbatim(void)
{
    const char *p;

    printf("test_recv_payload_verbatim\n");

    setup();
    lmqtt_rx_feed(&g_ctx, "+LMQTTURC: RECV,0,1,\"t\",\"a\",1\r\n",
                  strlen("+LMQTTURC: RECV,0,1,\"t\",\"a\",1\r\n"));
    p = lmqtt_take_downlink(&g_ctx);
    CHECK(p != NULL && strcmp(p, "\"a\",1") == 0,
          "非整体引号包裹的 payload 必须逐字节原样");

    setup();
    lmqtt_rx_feed(&g_ctx, "+LMQTTURC: RECV,0,1,\"t\",\"hello\"\r\n",
                  strlen("+LMQTTURC: RECV,0,1,\"t\",\"hello\"\r\n"));
    p = lmqtt_take_downlink(&g_ctx);
    CHECK(p != NULL && strcmp(p, "hello") == 0,
          "整体被引号包裹时应剥掉这一对");
}

/* 只有 topic、没有 payload：不产生"空串下行"（消费侧会拿它去解析 JSON） */
static void test_recv_topic_only_no_downlink(void)
{
    printf("test_recv_topic_only_no_downlink\n");
    setup();

    lmqtt_rx_feed(&g_ctx, "+LMQTTURC: RECV,0,1,\"t\"\r\n",
                  strlen("+LMQTTURC: RECV,0,1,\"t\"\r\n"));

    CHECK(lmqtt_take_downlink(&g_ctx) == NULL, "无 payload 时不应投递");
    CHECK(lmqtt_downlink_drops(&g_ctx) == 0, "这不算丢弃");
}

/* 结果 URC 先于 OK 到达、且信号量已被消耗：不能把到手的成功判成超时。
   （旧实现只看 sem_take 的返回值，会返回 LMQTT_ERR_TIMEOUT） */
static void test_urc_only_no_false_timeout(void)
{
    lmqtt_cmd_out_t out = { 0 };
    int32_t         rc;

    printf("test_urc_only_no_false_timeout\n");
    setup();
    reset_steps(NULL, NULL, NULL);      /* 不再喂任何回包：OK 永不到达 */

    CHECK(lmqtt_cmd_begin(&g_ctx, LMQTT_CMD_PUB, 5) == LMQTT_OK, "begin 应成功");

    lmqtt_rx_feed(&g_ctx, "+LMQTTPUB: 0,5,0\r\n",
                  strlen("+LMQTTPUB: 0,5,0\r\n"));

    /* 把 URC 给出的 token 消耗掉，构造出"got=true 且 sem 为空"的状态 */
    (void)mock_sem_take(NULL, 0);

    rc = lmqtt_cmd_finish(&g_ctx, LMQTT_TMO_ACK, LMQTT_TMO_PUB, &out);

    CHECK(rc == LMQTT_OK, "结果 URC 已到就不该报超时");
    CHECK(out.result == LMQTT_RES_OK, "result 应为 0");
}

/* 退订 URC 的手册拼写差异（+LMQTTUNSUNSUB）也应被识别 */
static void test_unsubscribe_urc_spelling(void)
{
    lmqtt_cmd_out_t out = { 0 };

    printf("test_unsubscribe_urc_spelling\n");
    setup();
    reset_steps("OK\r\n", "+LMQTTUNSUNSUB: 0,3,0\r\n", NULL);

    int32_t rc = lmqtt_cmd_exec(&g_ctx, LMQTT_CMD_SUBUNSUB, 3,
                                LMQTT_TMO_ACK, LMQTT_TMO_SUBUNSUB, &out,
                                "AT+LMQTTSUBUNSUB=0,1,3,\"t\",2");
    CHECK(rc == LMQTT_OK, "退订 URC 拼写差异应被兼容");
    CHECK(out.result == 0, "result 应为 0");
}

/* 配对字 (kind, msgid) 的原子发布语义
 *
 * mock 的 sem_take 在调用者线程内同步喂入模组响应（见 mock_sem_take 注释），
 * 全程单线程、无抢占，因此结构上**无法**构造出「接收侧快照与命令侧发布真正
 * 交错」的时序。这里退而守住新引入的不变量：配对字能整字编解码、abort 不
 * 残留 msgid、begin 先复位结果槽再发布。
 * 其中「abort 不残留 msgid」在旧实现（kind/msgid 是两个独立字段、abort 只清
 * kind）上必然失败。 */
static void test_cmd_pair_atomic(void)
{
    printf("test_cmd_pair_atomic\n");

    CHECK(LMQTT_PEND_KIND(LMQTT_CMD_PEND(LMQTT_CMD_PUB, 5)) == LMQTT_CMD_PUB,
          "配对字应能还原 kind");
    CHECK(LMQTT_PEND_MSGID(LMQTT_CMD_PEND(LMQTT_CMD_PUB, 0xFFFFu)) == 0xFFFFu,
          "msgid 边界值不应溢出到 kind 位");
    CHECK(LMQTT_PEND_KIND(LMQTT_CMD_PEND(LMQTT_CMD_NONE, 0)) == LMQTT_CMD_NONE,
          "空配对应能还原为 NONE");

    setup();
    CHECK(lmqtt_cmd_begin(&g_ctx, LMQTT_CMD_PUB, 7) == LMQTT_OK, "begin 应成功");
    CHECK(LMQTT_PEND_KIND(g_ctx.cmd.pending) == LMQTT_CMD_PUB,
          "begin 应发布 kind");
    CHECK(LMQTT_PEND_MSGID(g_ctx.cmd.pending) == 7, "begin 应发布 msgid");
    CHECK(g_ctx.cmd.result == -1 && !g_ctx.cmd.got && g_ctx.cmd.delivered == 0,
          "begin 应在发布配对字之前先复位结果槽");

    lmqtt_cmd_abort(&g_ctx);
    CHECK(LMQTT_PEND_KIND(g_ctx.cmd.pending) == LMQTT_CMD_NONE,
          "abort 后 kind 应归 NONE");
    CHECK(LMQTT_PEND_MSGID(g_ctx.cmd.pending) == 0,
          "abort 后不应残留 msgid（残留会让下一条同 kind 命令认错结果）");

    /* 无命令在途（配对为 NONE）时，结果 URC 不该被采纳，也不该唤醒命令侧 */
    g_sem_count = 0;
    lmqtt_rx_feed(&g_ctx, "+LMQTTPUB: 0,7,0\r\n", strlen("+LMQTTPUB: 0,7,0\r\n"));
    CHECK(g_sem_count == 0, "无命令在途时结果 URC 不应唤醒命令侧");
}

/* 结果槽里署着上一条命令的结果时，当前命令不得认领它
 *
 * 对应接收侧被抢占的时序：它为命令 N 写好了结果，却在命令 N 超时放弃、
 * 命令 N+1 已经开始之后才把这些字段写回。若只认 got，N+1 会把 N 的结果
 * 当成自己的成功（result 被读成 0、rc 返回 OK）。 */
static void test_stale_result_not_claimed(void)
{
    lmqtt_cmd_out_t out = { 0 };

    printf("test_stale_result_not_claimed\n");
    setup();

    /* 先跑完一条命令，让结果槽里留下它署名的结果 */
    reset_steps("OK\r\n", "+LMQTTPUB: 0,5,0\r\n", NULL);
    CHECK(lmqtt_cmd_exec(&g_ctx, LMQTT_CMD_PUB, 5,
                         LMQTT_TMO_ACK, LMQTT_TMO_PUB, &out,
                         "AT+LMQTTPUB=...") == LMQTT_OK,
          "第一条命令应成功");

    /* 下一条命令开始之后，接收侧才把上一条的结果写回：got 被置位，
       但署名（delivered）仍是上一条命令的配对字 */
    lmqtt_cmd_begin(&g_ctx, LMQTT_CMD_PUB, 6);
    g_ctx.cmd.result    = LMQTT_RES_OK;
    g_ctx.cmd.extra     = 0;
    g_ctx.cmd.delivered = LMQTT_CMD_PEND(LMQTT_CMD_PUB, 5);
    g_ctx.cmd.got       = true;

    CHECK(lmqtt_cmd_finish(&g_ctx, LMQTT_TMO_ACK, LMQTT_TMO_PUB, &out)
              == LMQTT_ERR_TIMEOUT,
          "署名不属于本命令的结果不得被认领");
}

/* 回执归属的计量（2026-09-30 追加）
 *
 * AT 的 OK/ERROR **不带命令标识**，所以"这份回执属于哪条命令"在协议层就做不到。
 * 引擎能回答的只有"当时有没有命令窗口"，据此把回执分成两类：
 *   stray_* —— 窗口外路过（无命令在途）；extra_* —— 同一窗口内第 2 条及以后。
 * 这几个计数是判断"要不要给命令窗口加栅栏"的唯一依据；数错了，后续决策就
 * 建立在一个假数字上，所以这里把每条语义边界都钉住。 */
static void test_ack_attribution_counters(void)
{
    lmqtt_err_counters_t ec;
    lmqtt_cmd_out_t      out = { 0 };
    int32_t              rc  = LMQTT_ERR_PARAM;

    printf("test_ack_attribution_counters\n");

    /* 1) CFG 窗口内的 OK 不得计成 stray —— 这就是 cmd.busy 存在的理由。
          CFG 的配对字（kind=NONE、msgid=0）字面值就是 0，与 abort 写回的空闲态
          **值碰撞**；用 pending 判窗口的写法会把 CFG 的整个窗口当成窗外。 */
    setup();
    CHECK(!g_ctx.cmd.busy, "未开窗时 busy 应为假");
    CHECK(lmqtt_cmd_begin(&g_ctx, LMQTT_CMD_NONE, 0) == LMQTT_OK, "begin 应成功");
    CHECK(g_ctx.cmd.pending == 0, "CFG 的配对字字面值就是 0（所以 pending 判不了窗口）");
    CHECK(g_ctx.cmd.busy, "begin 应开窗");
    lmqtt_rx_feed(&g_ctx, "OK\r\n", 4);
    lmqtt_get_err_counters(&g_ctx, &ec);
    CHECK(ec.stray_oks == 0, "CFG 窗口内的 OK 不得计成 stray_oks");
    CHECK(ec.extra_acks == 0, "本窗第 1 条 OK 不算 extra");
    lmqtt_cmd_abort(&g_ctx);
    CHECK(!g_ctx.cmd.busy, "abort 应关窗");

    /* 2) 窗口外到达的回执 */
    setup();
    lmqtt_rx_feed(&g_ctx, "OK\r\n", 4);
    lmqtt_rx_feed(&g_ctx, "ERROR\r\n", 7);
    lmqtt_get_err_counters(&g_ctx, &ec);
    CHECK(ec.stray_oks == 1, "无窗口在途的 OK 应计 stray_oks");
    CHECK(ec.stray_errs == 1, "无窗口在途的 ERROR 应计 stray_errs");
    CHECK(ec.extra_acks == 0 && ec.extra_errs == 0,
          "窗口外的行不得计进 extra_*（那是窗内语义）");

    /* 3) 同一窗口内第 2 条及以后（走真实时序：begin → 写出 → finish） */
    setup();
    reset_steps("OK\r\n", "OK\r\n+LMQTTPUB: 0,5,0\r\n", NULL);
    rc = lmqtt_cmd_exec(&g_ctx, LMQTT_CMD_PUB, 5,
                        LMQTT_TMO_ACK, LMQTT_TMO_PUB, &out, "AT+LMQTTPUB=...");
    CHECK(rc == LMQTT_OK, "窗内多一条 OK 不应改变命令结果");
    CHECK(out.result == LMQTT_RES_OK, "结果仍应来自真正的结果 URC");
    lmqtt_get_err_counters(&g_ctx, &ec);
    CHECK(ec.extra_acks == 1, "本窗第 2 条 OK 应计 extra_acks");
    CHECK(ec.early_acks == 0, "这条 OK 在写出完成后到达，不得计 early_acks");
    CHECK(ec.stray_oks == 0, "它在窗口内，不得计进 stray_oks");

    setup();
    reset_steps("OK\r\n", "ERROR\r\nERROR\r\n+LMQTTPUB: 0,5,0\r\n", NULL);
    rc = lmqtt_cmd_exec(&g_ctx, LMQTT_CMD_PUB, 5,
                        LMQTT_TMO_ACK, LMQTT_TMO_PUB, &out, "AT+LMQTTPUB=...");
    CHECK(rc == LMQTT_OK,
          "urc 段混进的 ERROR 不改变结果（rejected 只在 ack 段之后看一次）");
    lmqtt_get_err_counters(&g_ctx, &ec);
    CHECK(ec.extra_errs == 1, "本窗第 2 条 ERROR 应计 extra_errs");
    CHECK(ec.stray_errs == 0, "它在窗口内，不得计进 stray_errs");

    /* 4) 窗外的 OK 不影响随后的命令（begin 会复位窗口与信号量） */
    setup();
    lmqtt_rx_feed(&g_ctx, "OK\r\n", 4);
    reset_steps("OK\r\n", "+LMQTTPUB: 0,5,0\r\n", NULL);
    rc = lmqtt_cmd_exec(&g_ctx, LMQTT_CMD_PUB, 5,
                        LMQTT_TMO_ACK, LMQTT_TMO_PUB, &out, "AT+LMQTTPUB=...");
    CHECK(rc == LMQTT_OK, "窗外的 OK 不应影响随后的命令");
    CHECK(out.result == LMQTT_RES_OK, "结果应来自本命令自己的结果 URC");

    /* 5) 无关行（durian 的 AT 响应、模组启动横幅）只计 rx_other，不算回执 */
    setup();
    lmqtt_rx_feed(&g_ctx, "+CSQ: 29,0\r\n", strlen("+CSQ: 29,0\r\n"));
    lmqtt_rx_feed(&g_ctx, "^boot.rom'v\r\n", strlen("^boot.rom'v\r\n"));
    lmqtt_get_err_counters(&g_ctx, &ec);
    CHECK(ec.rx_other == 2, "既非 +LMQTT 也非终结符的行应计 rx_other");
    CHECK(ec.stray_oks == 0 && ec.stray_errs == 0, "它们不是回执，不得计进 stray_*");

    /* 6) 配对字不符的迟到结果 URC */
    setup();
    lmqtt_cmd_begin(&g_ctx, LMQTT_CMD_PUB, 5);
    lmqtt_rx_feed(&g_ctx, "+LMQTTPUB: 0,99,0\r\n", strlen("+LMQTTPUB: 0,99,0\r\n"));
    lmqtt_get_err_counters(&g_ctx, &ec);
    CHECK(ec.stale_urcs == 1, "msgid 不符的结果 URC 应计 stale_urcs");
    lmqtt_cmd_abort(&g_ctx);

    /* 7) 开窗之后、命令**写出之前**到达的 OK —— 模组那时还没收到命令，
          所以这条回执**必然不是本命令的**。这是"串门"唯一的直接证据，
          与 extra_*（同窗第 2 条）和 stray_*（窗外）都不同。 */
    setup();
    lmqtt_cmd_begin(&g_ctx, LMQTT_CMD_PUB, 5);
    CHECK(!g_ctx.cmd.written, "begin 之后、写出完成之前 written 应为假");
    lmqtt_rx_feed(&g_ctx, "OK\r\n", 4);
    lmqtt_get_err_counters(&g_ctx, &ec);
    CHECK(ec.early_acks == 1, "命令写出前到达的 OK 应计 early_acks");
    CHECK(ec.extra_acks == 0, "early 不是本窗第 2 条，不得计进 extra_acks");
    CHECK(ec.stray_oks == 0, "它在窗口内，不得计进 stray_oks");
    lmqtt_cmd_abort(&g_ctx);

    /* 8) 正常时序（先写完再等）不得计 early —— 守住上面那条的正向对照，
          否则 early_* 会退化成"每个窗口都 +1"的噪声计数器 */
    setup();
    reset_steps("OK\r\n", "+LMQTTPUB: 0,5,0\r\n", NULL);
    rc = lmqtt_cmd_exec(&g_ctx, LMQTT_CMD_PUB, 5,
                        LMQTT_TMO_ACK, LMQTT_TMO_PUB, &out, "AT+LMQTTPUB=...");
    CHECK(rc == LMQTT_OK, "正常命令应成功");
    lmqtt_get_err_counters(&g_ctx, &ec);
    CHECK(ec.early_acks == 0, "正常时序不得计 early_acks");
}

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== lmqtt core tests ===\n");
    test_pub_ok();
    test_pub_ack_err();
    test_stale_msgid_ignored();
    test_urc_before_ok();
    test_rejected();
    test_cfg_simple();
    test_recv_downlink();
    test_recv_cache_mode();
    test_stats();
    test_overflow_line_dropped();
    test_downlink_busy_drop();
    test_take_survives_next_put();
    test_recv_payload_verbatim();
    test_recv_topic_only_no_downlink();
    test_urc_only_no_false_timeout();
    test_unsubscribe_urc_spelling();
    test_cmd_pair_atomic();
    test_stale_result_not_claimed();
    test_ack_attribution_counters();

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
