#ifndef _MODULE_UDP_TYPES_h
#define _MODULE_UDP_TYPES_h

// ============================================================
// module_udp_types.h — типы, структуры и define'ы модуля UDP.
// Реализация: module_udp.cpp (шаблон) и module_udp_engine.cpp
// (шина UDP и связь с сервером).
// ============================================================

#include <Arduino.h>
#include <stdint.h>

#define CONFIG_FILE_UDP     "/config_udp.json"
#define HTML_FILE_UDP       "/udp.html"
#define UDP_DATA_LENGHT_MAX 64      // bytes for receive
#define UDP_DATA_MESSAGE_LEN 1024    // bytes for send

#define UDP_PORT  40001
#define UDP_BROADCAST_PORT_DFLT     40000
#define UDP_BROADCAST_TIME_DFLT     5
#define UDP_BROADCAST_KEYWORD_DFLT  "Ave_Omnissiah"
#define UDP_BROADCAST_POWERON       true
#define UDP_BROADCAST_RESPONSE       true

// Сколько подряд неподтверждённых unicast-телеметрий серверу нужно пропустить,
// чтобы устройство вернулось к бродкастам (discover/fallback).
#define UDP_MISS_LIMIT_DFLT     2
#define UDP_MISS_LIMIT_MIN      1
#define UDP_MISS_LIMIT_MAX      10

typedef struct {
    String keyword;
    uint16_t udpPortTx;
    uint16_t udpPortRx;
    uint16_t udpTimeOut;
    uint8_t  udpMissLimit;
    bool  udpPowerOn;
    bool  udpResponse;
} strUdpConfig;

#endif // _MODULE_UDP_TYPES_h
