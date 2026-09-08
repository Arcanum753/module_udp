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
#include <core_ntp/NtpClientLib.h>
#include "core_json/core_json.h"
#include "module_udp/module_udp.h"
#include "core_sys/eertos.h"
#include "common/common.h"
#include "module_udp_version.h"

// Единственный глобальный объект - сам класс
CLASS_MODULE_UDPBROADCAST module_udp(UDP_PORT);

CLASS_MODULE_UDPBROADCAST::CLASS_MODULE_UDPBROADCAST(uint16_t portListen) {
    _portRx = portListen;
    _portTx = portListen + 1;
    _isStarted = false;
    _isSending = false;
    _lastSendTime = 0;
    resetServerState();
}

// ============================================================
// begin()
// ============================================================
void CLASS_MODULE_UDPBROADCAST::begin() {
    defaultConfigUDP();
    if ( load_config_UDP() == false) {save_configUDP();}
    DEBUGUDP("%s\r\n", __FUNCTION__);
    if (_isStarted){    return;    }
    
    _portRx = _udpConfig.udpPortRx;
    
    if (_udp.listen(_portRx) == true) {
        DEBUGUDP("UDP Listening on IP: %s and port %u\n\r",  WiFi.localIP().toString().c_str(), _portRx);
        _isStarted = true;
        resetServerState();
        _udp.onPacket(processListenPacket);
    }
    SetTimerTask(broadcastTimer, SEC * MINUTES * module_udp.getTimeOut());
}

// ============================================================
// web_Init()
// ============================================================
void CLASS_MODULE_UDPBROADCAST::web_Init(void) {

    
    ESPHTTPServer.on(HTML_FILE_UDP, HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        get_configuration_html(request);
    });
    
    ESPHTTPServer.on("/udp/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        send_configuration_values_html(request);
    });
    
    ESPHTTPServer.on("/udp/test", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        broadcastTest(request);
    });

    ESPHTTPServer.on("/udp/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {return request->requestAuthentication(); }
        send_status_values_html(request);
    });

    ESPHTTPServer.on("/udp/ver", [this](AsyncWebServerRequest *request) {
        html_ver_get(request);
    });
}

// ============================================================
// Веб-обработчики
// ============================================================

// ========== SEND CONFIG HTML ==========
void CLASS_MODULE_UDPBROADCAST::send_configuration_values_html(AsyncWebServerRequest *request) {
    DEBUGUDP("%s\n\r", __FUNCTION__);
    String values = "";
    values += "udpporttx|"   + String(_udpConfig.udpPortTx) + "|input\n";
    values += "udpportrx|"   + String(_udpConfig.udpPortRx) + "|input\n";
    values += "udptime|"     + String(_udpConfig.udpTimeOut) + "|input\n";
    values += "udpkeyword|"  + _udpConfig.keyword + "|input\n";
    values += "udppoweron|"  + String(_udpConfig.udpPowerOn ? "checked" : "") + "|chk\n";
    values += "udpresponse|"  + String(_udpConfig.udpResponse ? "checked" : "") + "|chk\n";
    values += "udpmisslimit|" + String(_udpConfig.udpMissLimit) + "|input\n";
    request->send(200, "text/plain", values);
}

// ========== GET CONFIG HTML ==========
void CLASS_MODULE_UDPBROADCAST::get_configuration_html(AsyncWebServerRequest *request) {
    DEBUGUDP("%s\n\r", __PRETTY_FUNCTION__);
    _udpConfig.udpPowerOn  = false; 
    _udpConfig.udpResponse = false;
    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            DEBUGUDP("Arg %d: %s %s\r\n", i, 
                     request->argName(i).c_str(), 
                     request->arg(i).c_str());
            
            if (request->argName(i) == "udpporttx") { _udpConfig.udpPortTx = request->arg(i).toInt(); }
            if (request->argName(i) == "udpportrx") { _udpConfig.udpPortRx = request->arg(i).toInt(); }
            if (request->argName(i) == "udptime")   { _udpConfig.udpTimeOut = request->arg(i).toInt(); }
            if (request->argName(i) == "udpkeyword") { _udpConfig.keyword = urldecode(request->arg(i)); }
            if (request->argName(i) == "udppoweron")  { _udpConfig.udpPowerOn = true;  }
            if (request->argName(i) == "udpresponse") { _udpConfig.udpResponse = true; }
            if (request->argName(i) == "udpmisslimit") { _udpConfig.udpMissLimit = constrain(request->arg(i).toInt(), UDP_MISS_LIMIT_MIN, UDP_MISS_LIMIT_MAX); }
        }
        
        request->send_P(200, "text/html", Page_GeneralUdp);
        save_configUDP();
        broadcastTimer();
    }
    else {
        ESPHTTPServer.handleFileRead(request->url(), request);
    }
}

// ============================================================
// Конфиг
// ============================================================

// ========== DEFAULT CONFIG ==========
void CLASS_MODULE_UDPBROADCAST::defaultConfigUDP() {
    _udpConfig.udpPortTx = UDP_BROADCAST_PORT_DFLT;
    _udpConfig.udpPortRx = UDP_BROADCAST_PORT_DFLT + 1;
    _udpConfig.udpTimeOut = UDP_BROADCAST_TIME_DFLT;
    _udpConfig.keyword = UDP_BROADCAST_KEYWORD_DFLT;
    _udpConfig.udpPowerOn = UDP_BROADCAST_POWERON;
    _udpConfig.udpResponse = UDP_BROADCAST_RESPONSE;
    _udpConfig.udpMissLimit = UDP_MISS_LIMIT_DFLT;
}

// ========== SAVE CONFIG ==========
bool CLASS_MODULE_UDPBROADCAST::save_configUDP() {
    DEBUGUDP("%s\n\r", __PRETTY_FUNCTION__);
    JsonDocument doc;
    core_json.jsonFileLoadDoc(CONFIG_FILE_UDP, doc);
    doc["udpPortTx"] = _udpConfig.udpPortTx;
    doc["udpPortRx"] = _udpConfig.udpPortRx;
    doc["udpTimeOut"] = _udpConfig.udpTimeOut;
    doc["udpkeyword"] = _udpConfig.keyword;
    doc["udpPowerOn"] = _udpConfig.udpPowerOn;
    doc["udpResponse"] = _udpConfig.udpResponse;
    doc["udpMissLimit"] = _udpConfig.udpMissLimit;
    return core_json.jsonFileSaveDoc(CONFIG_FILE_UDP, doc);
}

// ========== LOAD CONFIG ==========
bool CLASS_MODULE_UDPBROADCAST::load_config_UDP() {
    DEBUGUDP("%s\n\r", __PRETTY_FUNCTION__);
    JsonDocument doc;
    if (!core_json.jsonFileLoadDoc(CONFIG_FILE_UDP, doc)) return false;
    _udpConfig.udpPortTx = doc["udpPortTx"].as<int>();
    _udpConfig.udpPortRx = doc["udpPortRx"].as<int>();
    _udpConfig.udpTimeOut = doc["udpTimeOut"].as<int>();
    _udpConfig.keyword = doc["udpkeyword"].as<String>();
    _udpConfig.udpPowerOn = doc["udpPowerOn"].as<bool>();
    _udpConfig.udpResponse = doc["udpResponse"].as<bool>();
    int missLimit = doc["udpMissLimit"].as<int>();
    _udpConfig.udpMissLimit = (missLimit >= UDP_MISS_LIMIT_MIN && missLimit <= UDP_MISS_LIMIT_MAX) ? (uint8_t)missLimit : UDP_MISS_LIMIT_DFLT;
    
    DEBUGUDP("updPortTx: %d\n\r", _udpConfig.udpPortTx);
    DEBUGUDP("updPortRx: %d\n\r", _udpConfig.udpPortRx);
    DEBUGUDP("udpTimeOut: %d\n\r", _udpConfig.udpTimeOut);
    DEBUGUDP("keyword: %s\n\r", _udpConfig.keyword.c_str());
    DEBUGUDP("udpPowerOn: %d\n\r", _udpConfig.udpPowerOn);
    DEBUGUDP("udpResponse: %d\n\r", _udpConfig.udpResponse);
    
    return true;
}

// ============================================================
// Версионные методы
// ============================================================

String CLASS_MODULE_UDPBROADCAST::getVersionStr(){
    return String(MODULE_UDP_VERSION);
}

String CLASS_MODULE_UDPBROADCAST::getGeneratedTime(){
    return String(MODULE_UDP_GENERATED_TIME);
}

String CLASS_MODULE_UDPBROADCAST::getCommitDateStr(){
    return String(MODULE_UDP_COMMIT_DATE_STR);
}

void CLASS_MODULE_UDPBROADCAST::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGUDP("%s\n\r", __FUNCTION__);
    String values = "";
    values += "udpversion|"     + getVersionStr()    + "|div\n";
    values += "udpgentime|"     + getGeneratedTime() + "|div\n";
    values += "udpgendate|"     + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}

// ============================================================
// Конкретная логика модуля
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

// ========== TEST ==========
void CLASS_MODULE_UDPBROADCAST::broadcastTest(AsyncWebServerRequest *request) {
    DEBUGUDP("%s\n\r", __FUNCTION__);
    broadcastSend(getPortTx(), jsonGet());
}

// ========== SERVER STATUS ==========
// Время последнего контакта с сервером для веб-страницы
String CLASS_MODULE_UDPBROADCAST::serverTimeStr() {
    if (_lastContactMs == 0) { return "never"; }
    // Если NTP синхронизирован и контакт был после синхронизации — абсолютное время
    if (_lastContactEpoch != 0) {
        return String(NTP.getTimeDateString((time_t)_lastContactEpoch));
    }
    // Иначе относительное время "назад"
    uint32_t deltaSec = (millis() - _lastContactMs) / 1000;
    if (deltaSec < 60) {
        return String(deltaSec) + "s ago";
    }
    if (deltaSec < 3600) {
        return String(deltaSec / 60) + "min " + String(deltaSec % 60) + "s ago";
    }
    return String(deltaSec / 3600) + "h " + String((deltaSec % 3600) / 60) + "min ago";
}

// ========== SEND STATUS HTML ==========
void CLASS_MODULE_UDPBROADCAST::send_status_values_html(AsyncWebServerRequest *request) {
    DEBUGUDP("%s\n\r", __FUNCTION__);
    String values = "";
    values += "udpserveronline|" + String(_serverOnline ? "Online" : "Offline") + "|div\n";
    values += "udpserverip|"     + (_serverOnline ? _serverIp.toString() : String("-")) + "|div\n";
    values += "udpservertime|"   + serverTimeStr() + "|div\n";
    values += "udpmode|"         + String(_serverOnline ? "unicast" : "broadcast") + "|div\n";
    request->send(200, "text/plain", values);
}

// ========== JSON GET ==========
String CLASS_MODULE_UDPBROADCAST::jsonGet() {
    String ret = "";
    ret += "{\n";
    ret += "  \"deviceName\": \"" + ESPHTTPServer._sysConfig.deviceName + "\",\n";
    ret += "  \"deviceSerial\": \"" + ESPHTTPServer._sysConfig.deviceSerial + "\",\n";
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
