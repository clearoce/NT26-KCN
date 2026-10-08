/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-03-30     chenyong     first version
 * 2018-08-17     chenyong     multiple client support
 */

#ifndef NT26_AT_H
#define NT26_AT_H

#include "at_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AT_SW_VERSION                  "1.3.1"

enum at_status
{
    AT_STATUS_UNINITIALIZED = 0,
    AT_STATUS_INITIALIZED,
    AT_STATUS_CLI,
};

typedef enum at_status at_status_t;

enum at_resp_status
{
     AT_RESP_OK = 0,                   /* AT response end is OK */
     AT_RESP_ERROR = -1,               /* AT response end is ERROR */
     AT_RESP_TIMEOUT = -2,             /* AT response is timeout */
     AT_RESP_BUFF_FULL= -3,            /* AT response buffer is full */
};
typedef enum at_resp_status at_resp_status_t;

struct at_response
{
    /* response buffer */
    char *buf;
    /* the maximum response buffer size, it set by `at_create_resp()` function */
    size_t buf_size;
    /* the length of current response buffer */
    size_t buf_len;
    /* the number of setting response lines, it set by `at_create_resp()` function
     * == 0: the response data will auto return when received 'OK' or 'ERROR'
     * != 0: the response data will return when received setting lines number data */
    size_t line_num;
    /* the count of received response lines */
    size_t line_counts;
    /* the maximum response time */
    int32_t timeout;
};

typedef struct at_response *at_response_t;

struct at_client;

typedef struct at_client_opt {
	int32_t (*ac_getc)(int32_t timeout);
	int32_t (*ac_putc)(int32_t c);
	int32_t (*ac_send)(const void *data, size_t len);
	int32_t (*ac_recv)(void *buf, size_t bufsz, int32_t timeout);
} at_client_opt_t;

struct at_client
{
    at_status_t status;

	/* 发送缓冲：at_obj_exec_cmd 在这里拼装命令。
	   命令的收发与配对全在 AT 通道引擎里完成，本对象不再持有互斥、信号量
	   与行缓冲 —— 那些都属于"AT 事务"，只能有一个所有者。 */
	char *send_buf;
    /* The maximum supported send cmd length */
    size_t send_bufsz;

    /* 接收搬运任务：只把串口字节转交给 AT 通道引擎，自身不做解析 */
    at_task_t parser;

	const at_client_opt_t *opt;
	void *user_data;
};
typedef struct at_client *at_client_t;

/* AT client initialize and start*/
int at_client_init(at_client_t client, size_t recv_bufsz, size_t send_bufsz,
                   int32_t prio, size_t stack_size,
                   const at_client_opt_t *opt, void *user_data);

/* ========================== multiple AT client function ============================ */

/* AT client wait for connection to external devices. */
int at_client_obj_wait_connect(at_client_t client, uint32_t timeout);

/*
 * 发送一条 AT 命令并等待其响应。
 *
 * resp 为 NULL 时只等 OK/ERROR；非 NULL 时把这次往返的**全部**响应行填进
 * resp->buf（各行以 '\0' 分隔，行内不含 CR/LF）。
 *
 * ⚠️ resp->line_num 只表达"期望几行"，**不参与终止判定** —— 一律以 OK/ERROR
 * 收尾。终止规则只有一条，就不必依赖"模组恰好回了几行"：旧实现把
 * line_num != 0 读作"收满 N 行才结束"，而空行也计入行数，能否收窗完全取决于
 * 模组如何分行（NT26 对 AT+CSQ 回的是含两个空行的四行）。
 *
 * 返回 0 成功；AT_ERR_PERM 模组拒绝（ERROR / +CME ERROR）；AT_ERR_TIMEOUT 超时。
 */
int at_obj_exec_cmd(at_client_t client, at_response_t resp, const char *cmd_expr, ...);

/* AT response object create and delete */
at_response_t at_create_resp(size_t buf_size, size_t line_num, int32_t timeout);
void at_delete_resp(at_response_t resp);
at_response_t at_resp_set_info(at_response_t resp, size_t buf_size, size_t line_num, int32_t timeout);

/* AT response line buffer get and parse response buffer arguments */
const char *at_resp_get_line(at_response_t resp, size_t resp_line);
const char *at_resp_get_line_by_kw(at_response_t resp, const char *keyword);
int at_resp_parse_line_args(at_response_t resp, size_t resp_line, const char *resp_expr, ...);
int at_resp_parse_line_args_by_kw(at_response_t resp, const char *keyword, const char *resp_expr, ...);

/* ========================== single AT client function ============================ */

#ifdef __cplusplus
}
#endif

#endif /* __AT_H__ */
