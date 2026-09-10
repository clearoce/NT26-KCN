/*
 * test_cmd.c - 指令章节的命令拼装测试
 *
 * 用 mock 移植层捕获实际发出的字节，逐条比对手册中的命令格式。
 * 命令字符串拼错（少个引号、参数顺序反了）是最容易发生又最难在真机上
 * 一眼看出的错误，这层测试专门盯它。
 */
#include "lmqtt.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* mock 移植层 */

static lmqtt_t     g_ctx;
static char        g_tx[1024];
static size_t      g_tx_len;
static int         g_sem_count;
static const char *g_resp;          /* 待喂入的模组响应（一次一件） */

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
static void  mock_sem_give(void *s)  { (void)s; g_sem_count++; }
static void  mock_sem_reset(void *s) { (void)s; g_sem_count = 0; }

static int32_t mock_sem_take(void *s, uint32_t timeout_ms)
{
    (void)s;
    (void)timeout_ms;

    if (g_sem_count > 0) {
        g_sem_count--;
        return 0;
    }
    if (g_resp != NULL) {
        lmqtt_rx_feed(&g_ctx, g_resp, strlen(g_resp));
        g_resp = NULL;
        if (g_sem_count > 0) {
            g_sem_count--;
            return 0;
        }
    }
    return 1;
}

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

static void setup(const char *resp)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_tx_len     = 0;
    g_sem_count  = 0;
    g_resp       = resp;
    lmqtt_init(&g_ctx, &g_port);
}

static void expect_tx(const char *want, const char *what)
{
    g_tx[g_tx_len] = '\0';
    if (strcmp(g_tx, want) == 0) {
        g_pass++;
    } else {
        g_fail++;
        printf("  FAIL: %s\n    期望: %s\n    实际: %s\n", what, want, g_tx);
    }
}

static void expect_rc(int32_t got, int32_t want, const char *what)
{
    if (got == want) {
        g_pass++;
    } else {
        g_fail++;
        printf("  FAIL: %s  (rc=%d 期望 %d)\n", what, (int)got, (int)want);
    }
}

/* ------------------------------------------------------------------ */
/* 用例 */

static void test_cfg(void)
{
    printf("test_cfg\n");
    setup("OK\r\n");
    expect_rc(lmqtt_cfg_cache(&g_ctx, LMQTT_CACHE_DIRECT), LMQTT_OK, "cfg cache");
    expect_tx("AT+LMQTTCFG=\"cache\",0,0\r\n", "cfg cache 命令格式");

    setup("OK\r\n");
    expect_rc(lmqtt_cfg_keepalive(&g_ctx, 180), LMQTT_OK, "cfg keepalive");
    expect_tx("AT+LMQTTCFG=\"keepalive\",0,180\r\n", "cfg keepalive 命令格式");

    setup("OK\r\n");
    expect_rc(lmqtt_cfg_recv_mode(&g_ctx, LMQTT_FMT_TEXT, false), LMQTT_OK, "cfg recv/mode");
    expect_tx("AT+LMQTTCFG=\"recv/mode\",0,0,0\r\n", "cfg recv/mode 命令格式");

    setup("OK\r\n");
    expect_rc(lmqtt_cfg_write(&g_ctx, "will", ",1,1,0,\"t/x\",\"off\""), LMQTT_OK, "cfg will");
    expect_tx("AT+LMQTTCFG=\"will\",0,1,1,0,\"t/x\",\"off\"\r\n", "cfg will 命令格式");

    /* keepalive 超出手册范围应被拒，且不产生任何发送 */
    setup("OK\r\n");
    expect_rc(lmqtt_cfg_keepalive(&g_ctx, 4000), LMQTT_ERR_PARAM, "keepalive 越界");
    expect_tx("", "越界参数不应发出命令");
}

static void test_open(void)
{
    printf("test_open\n");
    setup("OK\r\n+LMQTTOPEN: 0,0\r\n");

    expect_rc(lmqtt_open(&g_ctx, "mqtt.clearoce.cc.cd", 1883, NULL), LMQTT_OK, "open");
    expect_tx("AT+LMQTTOPEN=0,\"mqtt.clearoce.cc.cd\",1883\r\n", "open 命令格式");
}

static void test_open_fail_reason(void)
{
    lmqtt_open_ext_t ext = { 0 };

    printf("test_open_fail_reason\n");
    /* result=-1, extend=4(DNS 解析失败) */
    setup("OK\r\n+LMQTTOPEN: 0,-1,4\r\n");

    expect_rc(lmqtt_open(&g_ctx, "bad.host", 1883, &ext), LMQTT_ERR_RESULT, "open 失败应报错");
    expect_rc((int32_t)ext, (int32_t)LMQTT_OPEN_EXT_DNS, "应回填 DNS 失败原因");
}

static void test_conn(void)
{
    lmqtt_conn_rc_t rc = { 0 };

    printf("test_conn\n");
    setup("OK\r\n+LMQTTCONN: 0,0,0\r\n");

    expect_rc(lmqtt_conn(&g_ctx, "861326070275653", "861326070275653",
                         "AECFC44D182082C38231A82D47FE8CED", &rc),
              LMQTT_OK, "conn");
    expect_tx("AT+LMQTTCONN=0,\"861326070275653\",\"861326070275653\","
              "\"AECFC44D182082C38231A82D47FE8CED\"\r\n", "conn 命令格式");
    expect_rc((int32_t)rc, (int32_t)LMQTT_CONN_ACCEPTED, "ret_code 应为 0");
    expect_rc(lmqtt_is_connected(&g_ctx) ? 1 : 0, 1, "成功后应转为已连接");
}

static void test_conn_rejected(void)
{
    lmqtt_conn_rc_t rc = { 0 };

    printf("test_conn_rejected\n");
    setup("OK\r\n+LMQTTCONN: 0,0,4\r\n");        /* ret_code=4 用户名或密码错误 */

    expect_rc(lmqtt_conn(&g_ctx, "id", "user", "bad", &rc),
              LMQTT_ERR_RESULT, "认证失败应报错");
    expect_rc((int32_t)rc, (int32_t)LMQTT_CONN_BAD_CREDENTIAL, "应回填拒绝原因");
    expect_rc(lmqtt_is_connected(&g_ctx) ? 1 : 0, 0, "失败不应标记为已连接");
}

static void test_subunsub(void)
{
    printf("test_subunsub\n");
    setup("OK\r\n+LMQTTSUBUNSUB: 0,1,0,1\r\n");
    expect_rc(lmqtt_subscribe(&g_ctx, 1, "standard-config/imei/var_write", LMQTT_QOS2),
              LMQTT_OK, "subscribe");
    expect_tx("AT+LMQTTSUBUNSUB=0,0,1,\"standard-config/imei/var_write\",2\r\n",
              "subscribe 命令格式");

    setup("OK\r\n+LMQTTUNSUNSUB: 0,2,0\r\n");    /* 退订 URC 的拼写差异 */
    expect_rc(lmqtt_unsubscribe(&g_ctx, 2, "standard-config/imei/var_write"),
              LMQTT_OK, "unsubscribe");
    expect_tx("AT+LMQTTSUBUNSUB=0,1,2,\"standard-config/imei/var_write\",0\r\n",
              "unsubscribe 命令格式");

    /* msgID 0 非法 */
    setup("OK\r\n");
    expect_rc(lmqtt_subscribe(&g_ctx, 0, "t", LMQTT_QOS1), LMQTT_ERR_PARAM, "msgID=0 应被拒");
}

static void test_pub(void)
{
    printf("test_pub\n");
    setup("OK\r\n+LMQTTPUB: 0,5,0\r\n");

    expect_rc(lmqtt_pub(&g_ctx, 5, LMQTT_QOS1, false,
                        "standard-config/imei/data", "abcd", 4, 0),
              LMQTT_OK, "pub");
    expect_tx("AT+LMQTTPUB=0,5,1,0,\"standard-config/imei/data\",4,\"abcd\"\r\n",
              "pub 命令格式（分段发送后应还原为完整命令）");
}

static void test_pub_json_payload(void)
{
    const char *json = "{\"imei\":\"861326070275653\",\"rssi\":27}";
    char        want[256];

    printf("test_pub_json_payload\n");
    setup("OK\r\n+LMQTTPUB: 0,7,0\r\n");

    expect_rc(lmqtt_pub(&g_ctx, 7, LMQTT_QOS1, false, "t", json, strlen(json), 0),
              LMQTT_OK, "含引号的 JSON payload 应原样发出");

    /* 长度由 strlen 算出，避免手数出错 */
    snprintf(want, sizeof(want), "AT+LMQTTPUB=0,7,1,0,\"t\",%u,\"%s\"\r\n",
             (unsigned)strlen(json), json);
    expect_tx(want, "JSON payload 不应被转义或截断");
}

static void test_pub_result_retrans(void)
{
    printf("test_pub_result_retrans\n");
    /* result=1 是重传，实测消息已到达 broker，应视为成功 */
    setup("OK\r\n+LMQTTPUB: 0,9,1,6\r\n");
    expect_rc(lmqtt_pub(&g_ctx, 9, LMQTT_QOS1, false, "t", "x", 1, 0),
              LMQTT_OK, "result=1（重传）应视为成功");

    setup("OK\r\n+LMQTTPUB: 0,9,2\r\n");
    expect_rc(lmqtt_pub(&g_ctx, 9, LMQTT_QOS1, false, "t", "x", 1, 0),
              LMQTT_ERR_RESULT, "result=2（失败）应报错");
}

static void test_pub_oversize(void)
{
    printf("test_pub_oversize\n");
    setup("OK\r\n");
    expect_rc(lmqtt_pub(&g_ctx, 1, LMQTT_QOS1, false, "t", "x",
                        LMQTT_PUB_INLINE_MAX + 1, 0),
              LMQTT_ERR_OVERFLOW, "超出 inline 上限应被拒");
    expect_tx("", "被拒的发布不应发出任何字节");
}

static void test_close(void)
{
    printf("test_close\n");
    setup("OK\r\n+LMQTTCLOSE: 0,0\r\n");
    g_ctx.connected = true;

    expect_rc(lmqtt_close(&g_ctx), LMQTT_OK, "close");
    expect_tx("AT+LMQTTCLOSE=0\r\n", "close 命令格式");
    expect_rc(lmqtt_is_connected(&g_ctx) ? 1 : 0, 0, "close 后应转为未连接");
}

static void test_stats_helper(void)
{
    printf("test_stats_helper\n");
    expect_rc(strcmp(lmqtt_stats_str(LMQTT_STATS_MQTT_REFUSED), "mqtt-refused"), 0,
              "stats 字符串");
    expect_rc(lmqtt_stats_needs_reconnect(LMQTT_STATS_OK) ? 1 : 0, 0, "stats=0 不需重连");
    expect_rc(lmqtt_stats_needs_reconnect(LMQTT_STATS_PEER_RESET) ? 1 : 0, 1,
              "服务器断开需重连");
}

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== lmqtt command tests ===\n");
    test_cfg();
    test_open();
    test_open_fail_reason();
    test_conn();
    test_conn_rejected();
    test_subunsub();
    test_pub();
    test_pub_json_payload();
    test_pub_result_retrans();
    test_pub_oversize();
    test_close();
    test_stats_helper();

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
