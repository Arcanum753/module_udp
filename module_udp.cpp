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
#include "core_sys/core_sys.h"
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
// Веб-интерфейс статуса и теста
// ============================================================

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
