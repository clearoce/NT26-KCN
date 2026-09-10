/*
 * lmqtt_urc.h - +LMQTTURC 模组主动上报
 *
 * URC 有两类，引擎已在内部处理，本文件只提供对宿主有用的辅助：
 *
 *   +LMQTTURC: RECV,<id>,<msgID>,"<topic>",<payload>   直吐模式
 *   +LMQTTURC: RECV,<id>,<msgID>                       缓存模式（需 LMQTTREAD）
 *   +LMQTTURC: STATS,<id>,<stats>[,<extend>]           链路层状态变化
 *
 * 下行 payload 经 lmqtt_take_downlink() 轮询取走（见 lmqtt_core.h）；
 * STATS 经 lmqtt_set_stats_cb() 注册的回调通知。
 */
#ifndef LMQTT_URC_H
#define LMQTT_URC_H 1

#include "lmqtt_types.h"
#include "lmqtt_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* STATS 状态码的可读名称（用于日志；未知值返回 "unknown"） */
const char *lmqtt_stats_str(lmqtt_stats_t stat);

/* 该状态是否意味着链路已不可用、需要重连。
   除 0（成功）外的状态码都伴随连接失效。 */
bool lmqtt_stats_needs_reconnect(lmqtt_stats_t stat);

#ifdef __cplusplus
}
#endif

#endif /* LMQTT_URC_H */
