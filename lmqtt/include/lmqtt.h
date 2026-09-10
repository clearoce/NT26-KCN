/*
 * lmqtt.h - NT26 模组 LMQTT 指令集封装库（总入口）
 *
 * 用法概览：
 *   1. 宿主实现 lmqtt_port_t（串口写 + 互斥/信号量）
 *   2. lmqtt_init(&ctx, &port)
 *   3. 串口中断/接收任务里把收到的字节交给 lmqtt_rx_feed()
 *   4. 业务任务调用各指令 API（lmqtt_cfg_* / lmqtt_open() / lmqtt_pub() ...）
 *   5. 下行 payload 用 lmqtt_take_downlink() 轮询取走，在业务任务自己的栈上解析
 *
 * 本库不含任何 MQTT 报文格式解析、JSON 处理或认证算法——它只负责把
 * 「AT 指令 ↔ 模组 URC」这层封装干净。
 */
#ifndef LMQTT_H
#define LMQTT_H 1

#include "lmqtt_types.h"
#include "lmqtt_port.h"
#include "lmqtt_core.h"

/* 指令章节（与《NT26 MQTT 协议指令》手册一一对应） */
#include "lmqtt_cfg.h"
#include "lmqtt_open.h"
#include "lmqtt_close.h"
#include "lmqtt_conn.h"
#include "lmqtt_subunsub.h"
#include "lmqtt_pub.h"
#include "lmqtt_urc.h"
/* 以下章节待实现（阶段 3）：
   lmqtt_disc.h     AT+LMQTTDISC
   lmqtt_pubex.h    AT+LMQTTPUBEX
   lmqtt_read.h     AT+LMQTTREAD
 */

#endif /* LMQTT_H */
