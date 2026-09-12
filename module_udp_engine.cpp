#include <Arduino.h>
#include "version.h"
#include "main.h"
#ifdef ESP32
#include <ESPmDNS.h>
#include <AsyncUDP.h>
#else
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncUDP.h>
#endif

#include <WiFiClient.h>
#include "core_ntp/NtpClientLib.h"
#include "core_json/core_json.h"
#include "core_sys/core_sys.h"
#include "module_udp/module_udp.h"
#include "core_sys/eertos.h"

// ============================================================
// Публичное API
// ============================================================

uint16_t CLASS_MODULE_UDPBROADCAST::getPortTx()    { return _udpConfig.udpPortTx; }
uint16_t CLASS_MODULE_UDPBROADCAST::getPortRx()    { return _udpConfig.udpPortRx; }
uint16_t CLASS_MODULE_UDPBROADCAST::getTimeOut()   { return _udpConfig.udpTimeOut; }
String CLASS_MODULE_UDPBROADCAST::getKeyword()     { return _udpConfig.keyword; }
bool CLASS_MODULE_UDPBROADCAST::powerOnGet()       { return _udpConfig.udpPowerOn; }
bool CLASS_MODULE_UDPBROADCAST::responseGet()       { return _udpConfig.udpResponse; }
uint8_t CLASS_MODULE_UDPBROADCAST::isStart()          { return _isStarted; }

// ========== STOP ==========
void CLASS_MODULE_UDPBROADCAST::stop() {
    DEBUGUDP("%s\r\n", __FUNCTION__);
    _isStarted = false;
    _serverOnline = false;
    _udp.close();
}

// ========== RESET SERVER STATE ==========
void CLASS_MODULE_UDPBROADCAST::resetServerState() {
    _serverOnline = false;
    _serverIp = IPAddress((uint32_t)0);
    _lastContactMs = 0;
    _lastContactEpoch = 0;
    _missCount = 0;
    _awaitAck = false;
}

// ========== SEND BROADCAST ==========
void CLASS_MODULE_UDPBROADCAST::broadcastSend(uint16_t _port, String _strin) {
    DEBUGUDP("%s\r\n", __FUNCTION__);
    if (_isStarted == false) {  return; }
    if (_strin.length() == 0 || _strin.length() >= UDP_DATA_MESSAGE_LEN) {  return; }
    if (_port == getPortRx()) { return; }
    
    // Проверка на зависшую отправку
    if (_isSending) {
        if (millis() - _lastSendTime > SEND_TIMEOUT) { _isSending = false; } 
        else { return; }
    }
    
    _isSending = true;
    _lastSendTime = millis();
    
    strlcpy(_sendBuffer, _strin.c_str(), sizeof(_sendBuffer));
    _sendBuffer[sizeof(_sendBuffer) - 1] = '\0';
    
    // ---- ОБХОД БАГА БИБЛИОТЕКИ ----
    // Создаем буфер в RAM и копируем туда данные принудительно
    char* ramBuffer = (char*)malloc(_strin.length() + 1);
    if (ramBuffer) {
        strcpy(ramBuffer, _sendBuffer);
        
        // Отправляем через AsyncUDPMessage
        AsyncUDPMessage msg(UDP_DATA_MESSAGE_LEN);
        msg.print(ramBuffer);
        _udp.broadcastTo(msg, _port);
        
        free(ramBuffer);
    } else {
        // Если не удалось выделить память, отправляем как обычно
        AsyncUDPMessage msg(UDP_DATA_MESSAGE_LEN);
        msg.print(_sendBuffer);
        _udp.broadcastTo(msg, _port);
    }
    // --------------------------------
    _isSending = false;
}

// ========== SEND UNICAST ==========
void CLASS_MODULE_UDPBROADCAST::unicastSend(IPAddress ip, uint16_t _port, String _strin) {
    DEBUGUDP("%s\r\n", __FUNCTION__);
    if (_isStarted == false) {  return; }
    if (_strin.length() == 0 || _strin.length() >= UDP_DATA_MESSAGE_LEN) {  return; }
    if ((uint32_t)ip == 0) { return; }
    if (_port == getPortRx()) { return; }
    
    // Проверка на зависшую отправку
    if (_isSending) {
        if (millis() - _lastSendTime > SEND_TIMEOUT) { _isSending = false; } 
        else { return; }
    }
    
    _isSending = true;
    _lastSendTime = millis();
    
    strlcpy(_sendBuffer, _strin.c_str(), sizeof(_sendBuffer));
    _sendBuffer[sizeof(_sendBuffer) - 1] = '\0';
    
    // Отправляем через AsyncUDPMessage (unicast на IP сервера)
    char* ramBuffer = (char*)malloc(_strin.length() + 1);
    if (ramBuffer) {
        strcpy(ramBuffer, _sendBuffer);
        AsyncUDPMessage msg(UDP_DATA_MESSAGE_LEN);
        msg.print(ramBuffer);
        _udp.sendTo(msg, ip, _port);
        free(ramBuffer);
    } else {
        AsyncUDPMessage msg(UDP_DATA_MESSAGE_LEN);
        msg.print(_sendBuffer);
        _udp.sendTo(msg, ip, _port);
    }
    _isSending = false;
}

// ========== SERVER HEARD ==========
// Запоминаем сервер и фиксируем время контакта (вызывается при ACK или probe)
void serverHeard(IPAddress serverIp) {
    if ((uint32_t)serverIp == 0) { return; }
    module_udp._serverIp = serverIp;
    module_udp._serverOnline = true;
    module_udp._lastContactMs = millis();
    module_udp._lastContactEpoch = (NTP.getLastNTPSync() > 0) ? (uint32_t)now() : 0;
    module_udp._missCount = 0;
    module_udp._awaitAck = false;
    DEBUGUDP("Server heard: %s\r\n", serverIp.toString().c_str());
}

// ========== RESPONSE HANDLER ==========
void responseHandler(IPAddress serverIp) {
    DEBUGUDP("%s\n\r", __FUNCTION__);
    
    // Unicast-телеметрия на IP сервера (порт TX — на нём сервер слушает)
    if ((uint32_t)serverIp == 0) { return; }
    module_udp.unicastSend(serverIp, module_udp.getPortTx(), module_udp.jsonGet());
}

// ========== SIMPLE BROADCAST (для вызова из других файлов) ==========
void broadcastSimple() {
    module_udp.broadcastSend(module_udp.getPortTx(), module_udp.jsonGet());
}

// ========== TIMER (для вызова из других файлов) ==========
void broadcastTimer() {
    uint16_t timeout = module_udp.getTimeOut();
    if (module_udp.isStart() == false){   return; }
    if (timeout > 60){ timeout = 60;}
    if (timeout == 0) { return;  }
    
    DEBUGUDP("Udp timeout %d min. ", timeout);
    SetTimerTask(broadcastTimer, SEC * MINUTES * timeout);
    
    // Сервер известен и жив -> unicast-телеметрия (бродкастов не шлём)
    if (module_udp._serverOnline && (uint32_t)module_udp._serverIp != 0) {
        if (module_udp._awaitAck == false) {
            module_udp._awaitAck = true;
            module_udp.unicastSend(module_udp._serverIp, module_udp.getPortTx(), module_udp.jsonGet());
            return;
        }
        // Предыдущая unicast-посылка осталась без ответа
        module_udp._missCount++;
        if (module_udp._missCount >= module_udp._udpConfig.udpMissLimit) {
            DEBUGUDP("Server no reply %u times - back to broadcast\r\n", module_udp._missCount);
            module_udp._serverOnline = false;
            module_udp._serverIp = IPAddress((uint32_t)0);
            module_udp._awaitAck = false;
            module_udp._missCount = 0;
            broadcastSimple();
        } else {
            module_udp.unicastSend(module_udp._serverIp, module_udp.getPortTx(), module_udp.jsonGet());
        }
        return;
    }
    
    // Сервер неизвестен -> бродкаст (discover/fallback)
    broadcastSimple();
}

// ========== JSON GET ==========
String CLASS_MODULE_UDPBROADCAST::jsonGet() {
    String ret = "";
    ret += "{\n";
    ret += "  \"deviceName\": \"" + core_sys.getDeviceName() + "\",\n";
    ret += "  \"deviceSerial\": \"" + core_sys.getDeviceSerial() + "\",\n";
    ret += "  \"ip\": \"" + WiFi.localIP().toString() + "\",\n";
    ret += "  \"mac\": \"" + WiFi.macAddress() + "\",\n";
    ret += "  \"udpPortTx\": " + String(_udpConfig.udpPortTx) + ",\n";
    ret += "  \"udpPortRx\": " + String(_udpConfig.udpPortRx) + ",\n";
    ret += "  \"udpTimeOut\": " + String(_udpConfig.udpTimeOut) + ",\n";
    ret += "  \"keyword\": \"" + _udpConfig.keyword + "\",\n";
    ret += "  \"target\": \"" + String(BUILD_ENV) + "\",\n";
    ret += "  \"buildtime\": \"" + String(BUILD_TIME) + "\",\n";
    ret += "  \"gitbranch\": \"" + String(GIT_BRANCH) + "\",\n";
    ret += "  \"gitcommit\": \"" + String(GIT_COMMIT) + "\",\n";
    ret += "  \"uptime\": \"" + String(NTP.getUptimeString()) + "\",\n";
    ret += "  \"rstreason\": \"" + ESPHTTPServer.getResetReason() + "\",\n";
    ret += "  \"espVer\": \"" + String(FIRMWARE_VERSION) + "\"\n";
    ret += "}\n";
    return ret;
}

// ========== LISTEN PACKET ==========
void processListenPacket(AsyncUDPPacket &packet) {
    DEBUGUDP("%s\r\n", __FUNCTION__);
    if (packet.length() == 0 || packet.length() > UDP_DATA_LENGHT_MAX) { return; } // если пакет пустой/длинный
    
    // Get data
    char udpDataBuf[UDP_DATA_LENGHT_MAX + 1]; // +1 для нуль-терминатора
    size_t dataLen = packet.length();
    for (size_t i = 0; i < dataLen; i++) {
        udpDataBuf[i] = (char)*(packet.data() + i);
    }
    udpDataBuf[dataLen] = '\0'; // Важно!
    
    // ---- ACK от сервера: {"keyword":"...","ack":true} ----
    if (udpDataBuf[0] == '{') {
        String ackKeyword;
        bool ack = false;
        if (core_json.jsonParseStr(udpDataBuf, "keyword", ackKeyword)
            && core_json.jsonParseBool(udpDataBuf, "ack", ack)
            && ack && ackKeyword == module_udp.getKeyword()) {
            DEBUGUDP("Server ACK from %s\r\n", packet.remoteIP().toString().c_str());
            serverHeard(packet.remoteIP());
        }
        return;
    }
    
    // ---- Probe от сервера: голый keyword ----
    uint16_t kwLen = module_udp.getKeyword().length();
    if (kwLen == 0 || dataLen != kwLen) { return; }
    if (strncmp(udpDataBuf, module_udp.getKeyword().c_str(), kwLen) != 0) { return; }
    
    // На ESP8266 isBroadcast() не распознаёт широковещательный адрес подсети (x.x.x.255),
    // на который сервер шлёт пробу через netifaces
    IPAddress subnetBcast;
    subnetBcast[0] = WiFi.localIP()[0] | ~WiFi.subnetMask()[0];
    subnetBcast[1] = WiFi.localIP()[1] | ~WiFi.subnetMask()[1];
    subnetBcast[2] = WiFi.localIP()[2] | ~WiFi.subnetMask()[2];
    subnetBcast[3] = WiFi.localIP()[3] | ~WiFi.subnetMask()[3];
    if (packet.isBroadcast() == false && packet.localIP() != subnetBcast) { return; } // если пакет не бродкаст
    
    // Сервер найден: запоминаем IP (независимо от udpResponse)
    serverHeard(packet.remoteIP());
    
    // Отвечаем телеметрией только если разрешено
    if (module_udp.responseGet() == true) {
        responseHandler(packet.remoteIP());
    }
}
