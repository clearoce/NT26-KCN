/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-03-30     chenyong     first version
 * 2018-04-12     chenyong     add client implement
 * 2018-08-17     chenyong     multiple client support
 * 2021-03-17     Meco Man     fix a buf of leaking memory
 * 2021-07-14     Sszl         fix a buf of leaking memory
 * 2026-10-08     GDS(HC32L19x port) 改为「响应对象工具 + 接收搬运」：
 *                                命令的收发与配对交给 at_port_t.cmd_exec_raw
 *                                指向的**唯一 AT 引擎**，本文件不再解析串口。
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "at.h"

/* ------------------------------------------------------------------ */
/* 移植层（见 at_port.h）——本文件不引用任何 OS/MCU 头 */

static const at_port_t *s_port;

void at_set_port(const at_port_t *port) { s_port = port; }
const at_port_t *at_get_port(void) { return s_port; }

#define AT_MALLOC(n)                    (s_port->malloc(n))
#define AT_FREE(p)                      (s_port->free(p))
#define AT_TASK_CREATE(n, p, st, f, a)  (s_port->task_create((n), (p), (st), (f), (a)))
#define AT_TICK()                       (s_port->get_tick())
#define AT_MS2TICK(ms)                  (s_port->ms2tick(ms))

/**
 * Create response object.
 *
 * @param buf_size the maximum response buffer size
 * @param line_num the number of setting response lines
 *         = 0: the response data will auto return when received 'OK' or 'ERROR'
 *        != 0: the response data will return when received setting lines number data
 * @param timeout the maximum response time
 *
 * @return != NULL: response object
 *          = NULL: no memory
 */
at_response_t at_create_resp(size_t buf_size, size_t line_num, int32_t timeout)
{
    at_response_t resp = NULL;

    resp = (at_response_t)AT_MALLOC(sizeof(struct at_response));
    if (resp == NULL)
    {
        AT_LOG(AT_LOG_ERROR, "AT create response object failed! No memory for response object!");
        return NULL;
    }

    resp->buf = (char *) AT_MALLOC(buf_size);
    if (resp->buf == NULL)
    {
        AT_LOG(AT_LOG_ERROR, "AT create response object failed! No memory for response buffer!");
        AT_FREE(resp);
        return NULL;
    }

    resp->buf_size = buf_size;
    resp->line_num = line_num;
    resp->line_counts = 0;
    resp->buf_len = 0;
    resp->timeout = timeout;

    return resp;
}

/**
 * Delete and free response object.
 *
 * @param resp response object
 */
void at_delete_resp(at_response_t resp)
{
    if (resp && resp->buf)
    {
        AT_FREE(resp->buf);
    }

    if (resp)
    {
        AT_FREE(resp);
        resp = NULL;
    }
}

/**
 * Set response object information
 *
 * @param resp response object
 * @param buf_size the maximum response buffer size
 * @param line_num the number of setting response lines
 *         = 0: the response data will auto return when received 'OK' or 'ERROR'
 *        != 0: the response data will return when received setting lines number data
 * @param timeout the maximum response time
 *
 * @return  != NULL: response object
 *           = NULL: no memory
 */
at_response_t at_resp_set_info(at_response_t resp, size_t buf_size, size_t line_num, int32_t timeout)
{
    AT_ASSERT(resp);

    if (resp->buf_size != buf_size)
    {
        resp->buf_size = buf_size;

		AT_FREE(resp->buf);

        resp->buf = (char *) AT_MALLOC(buf_size);
        if (resp->buf == NULL)
        {
            AT_LOG(AT_LOG_ERROR, "No memory for realloc response buffer size(%d).", buf_size);
            return NULL;
        }
    }

    resp->line_num = line_num;
    resp->timeout = timeout;

    return resp;
}

/**
 * Get one line AT response buffer by line number.
 *
 * @param resp response object
 * @param resp_line line number, start from '1'
 *
 * @return != NULL: response line buffer
 *          = NULL: input response line error
 */
const char *at_resp_get_line(at_response_t resp, size_t resp_line)
{
    char *resp_buf = resp->buf;
    size_t line_num = 1;

    AT_ASSERT(resp);

    if (resp_line > resp->line_counts || resp_line <= 0)
    {
        AT_LOG(AT_LOG_ERROR, "AT response get line failed! Input response line(%d) error!", resp_line);
        return NULL;
    }

    for (line_num = 1; line_num <= resp->line_counts; line_num++)
    {
        if (resp_line == line_num)
        {
            return resp_buf;
        }

        resp_buf += strlen(resp_buf) + 1;
    }

    return NULL;
}

/**
 * Get one line AT response buffer by keyword
 *
 * @param resp response object
 * @param keyword query keyword
 *
 * @return != NULL: response line buffer
 *          = NULL: no matching data
 */
const char *at_resp_get_line_by_kw(at_response_t resp, const char *keyword)
{
    char *resp_buf = resp->buf;
    size_t line_num = 1;

    AT_ASSERT(resp);
    AT_ASSERT(keyword);

    for (line_num = 1; line_num <= resp->line_counts; line_num++)
    {
        if (strstr(resp_buf, keyword))
        {
            return resp_buf;
        }

        resp_buf += strlen(resp_buf) + 1;
    }

    return NULL;
}

/**
 * Get and parse AT response buffer arguments by line number.
 *
 * @param resp response object
 * @param resp_line line number, start from '1'
 * @param resp_expr response buffer expression
 *
 * @return -1 : input response line number error or get line buffer error
 *          0 : parsed without match
 *         >0 : the number of arguments successfully parsed
 */
int at_resp_parse_line_args(at_response_t resp, size_t resp_line, const char *resp_expr, ...)
{
    va_list args;
    int resp_args_num = 0;
    const char *resp_line_buf = NULL;

    AT_ASSERT(resp);
    AT_ASSERT(resp_expr);

    if ((resp_line_buf = at_resp_get_line(resp, resp_line)) == NULL)
    {
        return -1;
    }

    va_start(args, resp_expr);

    resp_args_num = vsscanf(resp_line_buf, resp_expr, args);

    va_end(args);

    return resp_args_num;
}

/**
 * Get and parse AT response buffer arguments by keyword.
 *
 * @param resp response object
 * @param keyword query keyword
 * @param resp_expr response buffer expression
 *
 * @return -1 : input keyword error or get line buffer error
 *          0 : parsed without match
 *         >0 : the number of arguments successfully parsed
 */
int at_resp_parse_line_args_by_kw(at_response_t resp, const char *keyword, const char *resp_expr, ...)
{
    va_list args;
    int resp_args_num = 0;
    const char *resp_line_buf = NULL;

    AT_ASSERT(resp);
    AT_ASSERT(resp_expr);

    if ((resp_line_buf = at_resp_get_line_by_kw(resp, keyword)) == NULL)
    {
        return -1;
    }

    va_start(args, resp_expr);

    resp_args_num = vsscanf(resp_line_buf, resp_expr, args);

    va_end(args);

    return resp_args_num;
}

/**
 * Send commands to AT server and wait response.
 *
 * @param client current AT client object
 * @param resp AT response object, using NULL when you don't care response
 * @param cmd_expr AT commands expression
 *
 * @return 0 : success
 *        AT_ERR_PERM : response status error (ERROR / +CME ERROR)
 *        AT_ERR_TIMEOUT : wait timeout
 *
 * @note 命令字符串里**不要**带 CRLF —— 补 CRLF 是 AT 通道引擎的事。
 */
int at_obj_exec_cmd(at_client_t client, at_response_t resp, const char *cmd_expr, ...)
{
    va_list args;
    size_t  len;
    int32_t rc;

    AT_ASSERT(cmd_expr);

    if (client == NULL)
    {
        AT_LOG(AT_LOG_ERROR, "input AT Client object is NULL, please create or get AT Client object!");
        return AT_ERR_PERM;
    }

    va_start(args, cmd_expr);
    len = vsnprintf(client->send_buf, client->send_bufsz, cmd_expr, args);
    va_end(args);

    /* vsnprintf 的返回值是"本该写入的长度"：截断时它 >= bufsz，
       此时缓冲里是半条命令，发出去只会换来一个 ERROR。 */
    if (len == 0 || len >= (size_t)client->send_bufsz)
    {
        AT_LOG(AT_LOG_ERROR, "command too long (%d >= %d)!", (int)len, (int)client->send_bufsz);
        return AT_ERR_NOBUF;
    }

    rc = s_port->cmd_exec_raw(client->send_buf,
                              (resp != NULL) ? (uint32_t)resp->timeout : (uint32_t)1000,
                              (resp != NULL) ? resp->buf : NULL,
                              (resp != NULL) ? resp->buf_size : 0,
                              (resp != NULL) ? &resp->buf_len : NULL,
                              (resp != NULL) ? &resp->line_counts : NULL);

    if (rc != AT_OK)
    {
        AT_LOG(AT_LOG_WARN, "execute command (%s) failed(%d)!", client->send_buf, (int)rc);
    }

    return rc;
}

/**
 * Waiting for connection to external devices.
 *
 * @param client current AT client object
 * @param timeout millisecond for timeout
 *
 * @return 0 : success
 *        AT_ERR_TIMEOUT : timeout
 *        AT_ERR_PERM : input AT Client object is NULL
 */
int at_client_obj_wait_connect(at_client_t client, uint32_t timeout)
{
    uint32_t start_time = 0;

    if (client == NULL)
    {
        AT_LOG(AT_LOG_ERROR, "input AT Client object is NULL, please create or get AT Client object!");
        return AT_ERR_PERM;
    }

    start_time = AT_TICK();

    while (1)
    {
        /* Check whether it is timeout */
        if (AT_TICK() - start_time > AT_MS2TICK(timeout))
        {
            AT_LOG(AT_LOG_ERROR, "wait AT client connect timeout(%d ms).", (int)timeout);
            return AT_ERR_TIMEOUT;
        }

        /* 一次探活就是一条裸 AT；单条给它 300ms，不占满外层 timeout。 */
        if (s_port->cmd_exec_raw("AT", 300, NULL, 0, NULL, NULL) == AT_OK)
        {
            return AT_OK;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 接收搬运
 *
 * 串口来的字节一个不留地转交给 AT 通道引擎，本任务**不做任何解析**。
 *
 * 为什么必须如此：AT 的 OK/ERROR 行不带命令标识，"这份回执属于哪条命令"只能
 * 靠"当时是谁的窗口"判定。只要还存在第二个自行组帧、自行配对的引擎，它的收尾
 * OK 就必然出现在另一个引擎的窗外（字节是扇出给两边的），归属就此失去定义。
 */

static void client_parser(at_client_t client)
{
    /* 栈上取 64：本任务只做搬运，不解析、不拼装 */
    uint8_t buf[64];

    for (;;)
    {
        int32_t n = client->opt->ac_recv(buf, sizeof(buf), -1);

        if (n > 0 && s_port->rx_feed != NULL)
        {
            s_port->rx_feed(buf, (size_t)n);
        }
    }
}

/* initialize the client object parameters */
static int at_client_para_init(at_client_t client, int32_t prio, size_t stack)
{
    int result = 0;

    client->status = AT_STATUS_UNINITIALIZED;

    client->send_buf = (char *) AT_MALLOC(client->send_bufsz);
    if (client->send_buf == NULL)
    {
        AT_LOG(AT_LOG_ERROR, "AT client initialize failed! No memory for send buffer.");
        result = AT_ERR_NOMEM;
        goto __exit;
    }

    client->parser = AT_TASK_CREATE("at_rx", prio, stack,
                             (void (*)(void *))client_parser, client);
    if (client->parser == NULL)
    {
        result = AT_ERR_NOMEM;
        goto __exit;
    }

__exit:
    if (result != 0)
    {
        if (client->send_buf)
        {
            AT_FREE(client->send_buf);
        }

        memset(client, 0x00, sizeof(struct at_client));
    }

    return result;
}

/**
 * AT client initialize.
 *
 * @param client the AT client object, must be allocated by the caller
 * @param recv_bufsz 接收缓冲长度；**已不再使用**（行缓冲归 AT 通道引擎所有），
 *                   保留在签名里只为不改动调用方
 * @param send_bufsz the maximum send command length
 * @param prio the priority of receiving task
 * @param stack_size the stack size of receiving task
 * @param opt the client port operations
 * @param user_data user data for the callbacks
 *
 * @return 0 : initialize success
 *        -1 : initialize failed
 *        -5 : no memory
 */
int at_client_init(at_client_t client, size_t recv_bufsz, size_t send_bufsz,
                   int32_t prio, size_t stack_size,
                   const at_client_opt_t *opt, void *user_data)
{
    int result = 0;

    AT_ASSERT(opt);
	AT_ASSERT(send_bufsz > 0);

    (void)recv_bufsz;

    /* 先清零再装参数：结构体本身来自调用方的 malloc，不置零的话
       未使用的字段会停在垃圾值上。 */
    memset(client, 0x00, sizeof(*client));

	client->opt = opt;
    client->send_bufsz = send_bufsz;
	client->user_data = user_data;

    result = at_client_para_init(client, prio, stack_size);
    if (result == 0)
    {
        client->status = AT_STATUS_INITIALIZED;
        AT_LOG(AT_LOG_INFO, "AT client(V%s) initialize success.", AT_SW_VERSION);
    }
    else
    {
        AT_LOG(AT_LOG_ERROR, "AT client(V%s) initialize failed(%d).", AT_SW_VERSION, result);
    }

    return result;
}
