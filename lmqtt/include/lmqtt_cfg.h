/*
 * lmqtt_cfg.h - AT+LMQTTCFG 配置可选参数
 *
 * 手册约束：will / session / keepalive / aliauth 需在 AT+LMQTTOPEN 之前配置。
 * 实测补充：**配置只对本次连接有效，CLOSE 后即失效** —— 每次 OPEN 前都要重写。
 */
#ifndef LMQTT_CFG_H
#define LMQTT_CFG_H 1

#include "lmqtt_types.h"
#include "lmqtt_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 接收数据的读取模式 <cache_mode> */
typedef enum lmqtt_cache_mode {
    LMQTT_CACHE_DIRECT = 0,     /* 直吐：RECV URC 直接携带 topic/payload */
    LMQTT_CACHE_BUFFER = 1,     /* 缓存：URC 仅携带 msgID，由 AT+LMQTTREAD 读取 */
} lmqtt_cache_mode_t;

/* 数据格式 <sendFormat> / <recvFormat> */
typedef enum lmqtt_data_format {
    LMQTT_FMT_TEXT     = 0,     /* 字符串（文本模式） */
    LMQTT_FMT_STR_HEX  = 1,     /* 字符串（HEX 模式） */
    LMQTT_FMT_HEX_TEXT = 2,     /* hex（文本模式） */
} lmqtt_data_format_t;

/* 配置接收数据的读取模式（直吐 / 缓存，见 lmqtt_cache_mode_t） */
int32_t lmqtt_cfg_cache(lmqtt_t *me, lmqtt_cache_mode_t mode);

/* 配置会话类型：0=断开后服务器保留订阅与 QoS1 下行队列；1=清除（Clean） */
int32_t lmqtt_cfg_session(lmqtt_t *me, uint8_t clean_session);

/* 配置保活时间（秒）。0 表示不断开；手册范围 0~3600，默认 120。
   注意服务器在 1.5 倍该时间内未收到消息即断开。 */
int32_t lmqtt_cfg_keepalive(lmqtt_t *me, uint16_t seconds);

/* 配置收/发数据格式 */
int32_t lmqtt_cfg_dataformat(lmqtt_t *me, lmqtt_data_format_t send_fmt,
                             lmqtt_data_format_t recv_fmt);

/* 配置 URC 上报开关。实测：置 1 后才有 +LMQTTPUB 等完成 URC */
int32_t lmqtt_cfg_urc(lmqtt_t *me, bool on);

/* 配置发送数据格式（等价于 dataformat 的发送部分） */
int32_t lmqtt_cfg_send_mode(lmqtt_t *me, lmqtt_data_format_t fmt);

/* 配置接收数据格式，并指定是否携带数据长度 <showLen> */
int32_t lmqtt_cfg_recv_mode(lmqtt_t *me, lmqtt_data_format_t fmt, bool show_len);

/* 配置 keepalive 期间是否快速进入 IDLE 省电模式 */
int32_t lmqtt_cfg_raimode(lmqtt_t *me, uint8_t mode);

/* 配置 SSL 模式（当前项目用明文 1883，如需 8883 需先上传 CA） */
int32_t lmqtt_cfg_sslenable(lmqtt_t *me, bool enable, uint8_t ssl_ctx_id);

/*
 * 通用配置写入，覆盖手册中本文件未单独封装的配置项（如 will / aliauth / cloud）。
 *   key  配置名，不含引号，如 "will"
 *   args 其后参数（含前导逗号），如 ",0" 或 ",\"topic\",\"msg\""
 * 例：lmqtt_cfg_write(me, "will", ",1,1,0,\"t/status\",\"offline\"");
 */
int32_t lmqtt_cfg_write(lmqtt_t *me, const char *key, const char *args);

#ifdef __cplusplus
}
#endif

#endif /* LMQTT_CFG_H */
