/*
 * lmqtt_open.h - AT+LMQTTOPEN 打开 MQTT 客户端网络
 *
 * 手册最大响应时间 160 秒（DNS + PDP 激活 + TCP 建链都可能耗在这里）。
 * 实测约束：配置（LMQTTCFG）只对本次连接有效，本命令之前必须重写全部配置。
 */
#ifndef LMQTT_OPEN_H
#define LMQTT_OPEN_H 1

#include "lmqtt_types.h"
#include "lmqtt_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 打开网络。
 *   host  服务器地址，IP 或域名，最长 LMQTT_HOST_MAX 字节
 *   port  服务器端口
 *   ext_out  可为 NULL；失败时回填扩展原因（DNS 失败 / PDP 失败 / 标识符被占用…）
 *
 * 返回 LMQTT_OK / LMQTT_ERR_RESULT（打开失败，原因见 ext_out）/ 其它负错误码。
 */
int32_t lmqtt_open(lmqtt_t *me, const char *host, uint16_t port,
                   lmqtt_open_ext_t *ext_out);

#ifdef __cplusplus
}
#endif

#endif /* LMQTT_OPEN_H */
