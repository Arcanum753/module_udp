#ifndef _MODULE_UDP_ENGINE_h
#define _MODULE_UDP_ENGINE_h

// ============================================================
// module_udp_engine.h — интерфейс исполнительной части module_udp
// (свободные функции, используемые шаблоном и другими модулями).
// Реализация: module_udp_engine.cpp
// ============================================================

#include <Arduino.h>
#include <IPAddress.h>

#ifdef ESP32
#include <AsyncUDP.h>
#endif

#ifdef ESP8266
#include <ESPAsyncUDP.h>
#endif

void broadcastSimple(void);
void broadcastTimer(void);

void processListenPacket(AsyncUDPPacket &packet);
void responseHandler(IPAddress serverIp);
void serverHeard(IPAddress serverIp);

#endif // _MODULE_UDP_ENGINE_h
