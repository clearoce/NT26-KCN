/*
 * lmqtt_urc.c - +LMQTTURC 辅助
 *
 * URC 的解析与分发在 lmqtt_core.c；这里只放便于宿主使用的查询辅助。
 */
#include "lmqtt_urc.h"

const char *lmqtt_stats_str(lmqtt_stats_t stat)
{
    switch (stat) {
    case LMQTT_STATS_OK:           return "ok";
    case LMQTT_STATS_PEER_RESET:   return "peer-reset";
    case LMQTT_STATS_URL_ERROR:    return "url-error";
    case LMQTT_STATS_DNS_FAIL:     return "dns-fail";
    case LMQTT_STATS_PROTO_ERROR:  return "proto-error";
    case LMQTT_STATS_HTTP_ERROR:   return "http-error";
    case LMQTT_STATS_CONN_TIMEOUT: return "conn-timeout";
    case LMQTT_STATS_CONN_ERROR:   return "conn-error";
    case LMQTT_STATS_FATAL:        return "fatal";
    case LMQTT_STATS_CLOSED:       return "closed";
    case LMQTT_STATS_NEED_MORE:    return "need-more";
    case LMQTT_STATS_CACHE_OVF:    return "cache-overflow";
    case LMQTT_STATS_SSL_FAIL:     return "ssl-fail";
    case LMQTT_STATS_MQTT_REFUSED: return "mqtt-refused";
    default:                       return "unknown";
    }
}

bool lmqtt_stats_needs_reconnect(lmqtt_stats_t stat)
{
    return stat != LMQTT_STATS_OK;
}
