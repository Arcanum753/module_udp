#ifndef _MODUDP_h
#define _MODUDP_h

#include "main.h"
#include "core_web/FSWebServerLib.h"

#ifdef ESP32
#include <AsyncUDP.h>
#endif

#ifdef ESP8266
#include <ESPAsyncUDP.h>
#endif

#ifdef DEBUG_UDP
#define DEBUGUDP(...) DBG_MOD("[M_UDP] ", __VA_ARGS__)
#else
#define DEBUGUDP(...)
#endif

const char Page_GeneralUdp[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/udp.html">
Please Wait....Configuring.
)=====";

#include "module_udp_types.h"
#include "module_udp_engine.h"

class CLASS_MODULE_UDPBROADCAST {
public:
    CLASS_MODULE_UDPBROADCAST(uint16_t portListen);

    void begin();
    void web_Init(void);

    // Публичное API
    void stop();
    void broadcastSend(uint16_t _port, String _str);
    void unicastSend(IPAddress ip, uint16_t _port, String _str);
    void broadcastTest(AsyncWebServerRequest *request);

    uint16_t getPortTx();
    uint16_t getPortRx();
    uint16_t getTimeOut();
    String getKeyword();
    bool powerOnGet();
    bool responseGet();
    String jsonGet();
    uint8_t isStart();

    // Статус связи с сервером
    bool serverOnlineGet()          { return _serverOnline; }
    IPAddress serverIpGet()         { return _serverIp; }
    uint32_t lastContactMsGet()     { return _lastContactMs; }
    String serverTimeStr();

    strUdpConfig _udpConfig; // UDP configuration

private:
    // Версионные методы
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void  html_ver_get(AsyncWebServerRequest *request);

    // Веб-обработчики
    void send_configuration_values_html(AsyncWebServerRequest *request);
    void get_configuration_html(AsyncWebServerRequest *request);
    void send_status_values_html(AsyncWebServerRequest *request);

    // Конфиг
    bool load_config_UDP();
    bool save_configUDP();
    void defaultConfigUDP();

    // Поля
    AsyncUDP _udp;

    char _sendBuffer[UDP_DATA_MESSAGE_LEN];

    bool _isSending;
    unsigned long _lastSendTime;
    static const unsigned long SEND_TIMEOUT = 500; // 500ms

    uint8_t _isStarted;
    uint16_t _portRx;
    uint16_t _portTx;

    // Состояние связи с сервером (unicast-режим)
    IPAddress _serverIp;
    bool _serverOnline;
    uint32_t _lastContactMs;      // millis() последнего ACK/probe от сервера
    uint32_t _lastContactEpoch;   // Unix-время контакта (если NTP синхронизирован)
    uint8_t _missCount;           // подряд пропущенных ответов сервера
    bool _awaitAck;               // ждём ACK на отправленную unicast-телеметрию

    void resetServerState();

    friend void broadcastTimer();
    friend void processListenPacket(AsyncUDPPacket &packet);
    friend void responseHandler(IPAddress serverIp);
    friend void serverHeard(IPAddress serverIp);
    friend void broadcastSimple(void);
};

extern CLASS_MODULE_UDPBROADCAST module_udp;

#endif // _MODUDP_h
