/*
 * lmqtt_close.h - AT+LMQTTCLOSE 关闭 MQTT 客户端网络
 *
 * 关闭后本次连接的所有 LMQTTCFG 配置即失效——再次使用前必须重新配置。
 */
#ifndef LMQTT_CLOSE_H
#define LMQTT_CLOSE_H 1

#include "lmqtt_types.h"
#include "lmqtt_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 关闭网络。返回 LMQTT_OK，或 LMQTT_ERR_RESULT（result=1 失败）及其它负错误码。
 * 无论模组是否回 ERROR 都会把本地连接状态置为未连接。
 */
int32_t lmqtt_close(lmqtt_t *me);

#ifdef __cplusplus
}
#endif

#endif /* LMQTT_CLOSE_H */
