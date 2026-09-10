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

/* PUB 结果为重传（手册 result=1）——引擎不判定业务语义，原样回填 */
static void test_pub_retrans(void)
{
    lmqtt_cmd_out_t out = { 0 };

    printf("test_pub_retrans\n");
    setup();
    reset_steps("OK\r\n", "+LMQTTPUB: 0,7,1,6\r\n", NULL);

    int32_t rc = lmqtt_cmd_exec(&g_ctx, LMQTT_CMD_PUB, 7,
                                LMQTT_TMO_ACK, LMQTT_TMO_PUB, &out, "AT+LMQTTPUB=...");

    CHECK(rc == LMQTT_OK, "命令应成功");
    CHECK(out.result == LMQTT_RES_RETRANS, "result 应为 1");
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

static void on_stats(lmqtt_t *me, lmqtt_stats_t stat, int32_t ext, void *user)
{
    (void)me; (void)ext; (void)user;
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

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== lmqtt core tests ===\n");
    test_pub_ok();
    test_pub_retrans();
    test_stale_msgid_ignored();
    test_urc_before_ok();
    test_rejected();
    test_cfg_simple();
    test_recv_downlink();
    test_recv_cache_mode();
    test_stats();
    test_overflow_line_dropped();
    test_downlink_busy_drop();
    test_unsubscribe_urc_spelling();

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
