#include "core_web/FSWebServerLib.h"

#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ESP32)
#include <WiFi.h>
#endif
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

#include "module_udp/module_udp.h"

#include "core_terminal/core_terminal.h"
#include "core_terminal/common_module.h"

// Строковые литералы терминала: на ESP8266 держим их в flash, иначе
// команда не влезает в DRAM (esp8266-udp занимает почти весь RAM).
#if defined(ESP8266)
#define UDP_LOG(fmt, ...) Serial.printf_P(PSTR(fmt), ##__VA_ARGS__)
#define UDP_LOG_LN(msg)   Serial.println(F(msg))
#else
#define UDP_LOG(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#define UDP_LOG_LN(msg)   Serial.println(msg)
#endif

// ============================================================
// Терминальные команды module_udp
//
// Одна команда верхнего уровня udp. Работа с конфигом (show/set/save)
// доступна всегда; сетевые действия (test/send/apply) — только при
// активном подключении. set меняет только RAM, на FS пишет save,
// к сокету применяет apply (правило apply/save).
// ============================================================

static const char* const kUdpCommands[] = {
    "show", "status", "send", "test", "set", "apply", "save", "stop", nullptr
};

static const char kUdpHelp[] PROGMEM =
    "udp - UDP broadcast and telemetry (changes are not saved automatically)\r\n"
    "  udp show                     config, socket state and server link\r\n"
    "  udp status                   server link state only\r\n"
    "  udp test                     send JSON telemetry broadcast (needs Wi-Fi)\r\n"
    "  udp send <text>              send text broadcast, 1..1023 bytes (needs Wi-Fi)\r\n"
    "  udp set <field> <value>      apply value in RAM only\r\n"
    "      fields: porttx|portrx 1..65535 (must differ)\r\n"
    "              timeout 0..60 min (0 = off)\r\n"
    "              keyword 1..63 printable, no spaces or quotes\r\n"
    "              poweron|response on|off\r\n"
    "              misslimit 1..10\r\n"
    "  udp apply                    reopen socket with current config (needs Wi-Fi)\r\n"
    "  udp save                     persist /config_udp.json\r\n"
    "  udp stop                     close socket until next Wi-Fi connect\r\n"
    "  udp help                     this help\r\n";

// Сеть для рассылок: сокет поднят и STA-подключение активно.
static bool udpLinkReady() {
    return (module_udp.isStart() != 0) && (WiFi.status() == WL_CONNECTED);
}

static const char* onOffText(bool on) {
    return on ? "on" : "off";
}

// Сокет не поднят: различаем «нет Wi-Fi» и «остановлен вручную» (udp stop).
static void printSocketStopped() {
    if (WiFi.status() == WL_CONNECTED) {
        UDP_LOG_LN("UDP socket stopped. Run 'udp apply'.");
    } else {
        UDP_LOG_LN("UDP not started (no Wi-Fi connection).");
    }
}

// ------------------------------------------------ show / status
static void cmdShow() {
    UDP_LOG("TX port:   %u\r\n", (unsigned)module_udp.getPortTx());
    UDP_LOG("RX port:   %u\r\n", (unsigned)module_udp.getPortRx());
    UDP_LOG("Timeout:   %u min\r\n", (unsigned)module_udp.getTimeOut());
    UDP_LOG("Keyword:   %s\r\n", module_udp.getKeyword().c_str());
    UDP_LOG("PowerOn:   %s\r\n", onOffText(module_udp.powerOnGet()));
    UDP_LOG("Response:  %s\r\n", onOffText(module_udp.responseGet()));
    UDP_LOG("MissLimit: %u\r\n", (unsigned)module_udp._udpConfig.udpMissLimit);

    if (module_udp.isStart()) {
        UDP_LOG("Socket:    listening on port %u\r\n", (unsigned)module_udp.getPortRx());
    } else if (WiFi.status() == WL_CONNECTED) {
        UDP_LOG_LN("Socket:    stopped (run 'udp apply')");
    } else {
        UDP_LOG_LN("Socket:    stopped (no Wi-Fi connection)");
    }

    if (module_udp.serverOnlineGet()) {
        UDP_LOG("Link:      unicast %s, last %s, miss %u\r\n",
                module_udp.serverIpGet().toString().c_str(),
                module_udp.serverTimeStr().c_str(),
                (unsigned)module_udp.missCountGet());
    } else {
        UDP_LOG_LN("Link:      broadcast (server unknown)");
    }

    if (module_udp.configDirtyGet()) {
        UDP_LOG_LN("Config not applied: run 'udp apply'.");
    }
}

static void cmdStatus() {
    if (module_udp.isStart() == 0) {
        printSocketStopped();
        return;
    }

    UDP_LOG("Socket:       listening on port %u\r\n", (unsigned)module_udp.getPortRx());
    UDP_LOG("Mode:         %s\r\n", module_udp.serverOnlineGet() ? "unicast" : "broadcast");
    if (module_udp.serverOnlineGet()) {
        UDP_LOG("Server:       %s (online)\r\n", module_udp.serverIpGet().toString().c_str());
    } else {
        UDP_LOG_LN("Server:       unknown (broadcast)");
    }
    UDP_LOG("Last contact: %s\r\n", module_udp.serverTimeStr().c_str());
    UDP_LOG("Miss count:   %u\r\n", (unsigned)module_udp.missCountGet());
    if (module_udp.configDirtyGet()) {
        UDP_LOG_LN("Config not applied: run 'udp apply'.");
    }
}

// ------------------------------------------------ test / send
static void cmdTest() {
    if (!udpLinkReady()) {
        printSocketStopped();
        return;
    }

    String payload = module_udp.jsonGet();
    UDP_LOG("Sending telemetry (%u bytes) to port %u...\r\n",
            (unsigned)payload.length(), (unsigned)module_udp.getPortTx());
    UDP_LOG_LN("--- payload ---");
    Serial.print(payload);
    UDP_LOG_LN("---------------");
    module_udp.broadcastSend(module_udp.getPortTx(), payload);
    UDP_LOG_LN("Sent.");
}

static void cmdSend() {
    String text = TerminalGetArgLine();
    if (text.length() == 0) {
        UDP_LOG_LN("Usage: udp send <text>");
        return;
    }
    if (text.length() >= UDP_DATA_MESSAGE_LEN) {
        UDP_LOG("Text too long (max %u bytes).\r\n", (unsigned)(UDP_DATA_MESSAGE_LEN - 1));
        return;
    }
    if (!udpLinkReady()) {
        printSocketStopped();
        return;
    }

    module_udp.broadcastSend(module_udp.getPortTx(), text);
    UDP_LOG("Sent %u bytes to port %u (broadcast).\r\n",
            (unsigned)text.length(), (unsigned)module_udp.getPortTx());
}

// ------------------------------------------------ set
// Keyword вставляется в JSON-телеметрию без экранирования: запрещаем
// кавычки и обратный слэш, а также пробелы/control (приёмный буфер 64 байта).
static bool keywordValid(const String& v) {
    if (v.length() == 0 || v.length() > 63) { return false; }
    for (size_t i = 0; i < v.length(); i++) {
        unsigned char c = (unsigned char)v[i];
        if (c < 0x21 || c > 0x7E || c == '"' || c == '\\') { return false; }
    }
    return true;
}

static void cmdSet() {
    char* field = term.getNext();
    if (field == nullptr) {
        UDP_LOG_LN("Usage: udp set <field> <value>");
        return;
    }

    String name  = String(field);
    String value = TerminalGetArgLine();

    if (name == "porttx" || name == "portrx") {
        long v = 0;
        if (!ns_core_terminal::parseIntStrict(value, v, 1, 65535)) {
            UDP_LOG_LN("Usage: udp set porttx|portrx <1..65535>");
            return;
        }
        uint16_t port  = (uint16_t)v;
        uint16_t other = (name == "porttx") ? module_udp.getPortRx()
                                            : module_udp.getPortTx();
        if (port == other) {
            UDP_LOG_LN("TX and RX ports must differ.");
            return;
        }
        if (name == "porttx") { module_udp._udpConfig.udpPortTx = port; }
        else                  { module_udp._udpConfig.udpPortRx = port; }
        module_udp.setConfigDirty();
        UDP_LOG("%s: %u (not applied; udp apply)\r\n",
                (name == "porttx") ? "Port TX" : "Port RX", (unsigned)port);
        return;
    }

    if (name == "timeout") {
        long v = 0;
        if (!ns_core_terminal::parseIntStrict(value, v, 0, 60)) {
            UDP_LOG_LN("Usage: udp set timeout <0..60> (minutes, 0 = off)");
            return;
        }
        module_udp._udpConfig.udpTimeOut = (uint16_t)v;
        module_udp.setConfigDirty();
        UDP_LOG("Timeout: %u min (not applied; udp apply)\r\n", (unsigned)v);
        return;
    }

    if (name == "keyword") {
        if (!keywordValid(value)) {
            UDP_LOG_LN("Usage: udp set keyword <1..63 printable, no spaces/quotes>");
            return;
        }
        module_udp._udpConfig.keyword = value;
        module_udp.setConfigDirty();
        UDP_LOG("Keyword: %s (not applied; udp apply)\r\n", value.c_str());
        return;
    }

    if (name == "poweron" || name == "response") {
        bool on = false;
        if (!ns_core_terminal::parseOnOff(value, on)) {
            UDP_LOG_LN("Usage: udp set poweron|response on|off");
            return;
        }
        if (name == "poweron") { module_udp._udpConfig.udpPowerOn = on; }
        else                   { module_udp._udpConfig.udpResponse = on; }
        module_udp.setConfigDirty();
        UDP_LOG("%s: %s (not applied; udp apply)\r\n",
                (name == "poweron") ? "PowerOn" : "Response", onOffText(on));
        return;
    }

    if (name == "misslimit") {
        long v = 0;
        if (!ns_core_terminal::parseIntStrict(value, v, UDP_MISS_LIMIT_MIN, UDP_MISS_LIMIT_MAX)) {
            UDP_LOG("Usage: udp set misslimit <%u..%u>\r\n",
                    (unsigned)UDP_MISS_LIMIT_MIN, (unsigned)UDP_MISS_LIMIT_MAX);
            return;
        }
        module_udp._udpConfig.udpMissLimit = (uint8_t)v;
        module_udp.setConfigDirty();
        UDP_LOG("MissLimit: %u (not applied; udp apply)\r\n", (unsigned)v);
        return;
    }

    UDP_LOG("Unknown udp field '%s'. Type 'udp help'.\r\n", name.c_str());
}

// ------------------------------------------------ apply / save / stop
static void cmdApply() {
    if (WiFi.status() != WL_CONNECTED) {
        UDP_LOG_LN("Wi-Fi not connected; config kept in RAM.");
        return;
    }
    if (module_udp.applyConfig()) {
        UDP_LOG_LN("Applied.");
    } else {
        UDP_LOG_LN("Apply failed (listen error).");
    }
}

static void cmdSave() {
    if (module_udp.saveConfigNow()) {
        UDP_LOG_LN("Saved.");
        if (module_udp.configDirtyGet()) {
            UDP_LOG_LN("Config not applied yet: run 'udp apply'.");
        }
    } else {
        UDP_LOG_LN("Save failed.");
    }
}

static void cmdStop() {
    if (module_udp.isStart() == 0) {
        UDP_LOG_LN("UDP already stopped.");
        return;
    }
    module_udp.stop();
    UDP_LOG_LN("Stopped until next Wi-Fi connect (or reboot).");
}

// ------------------------------------------------ регистрация
void CLASS_MODULE_UDPBROADCAST::terminalRegister() {
    TerminalRegisterModule("module_udp", "udp", "UDP broadcast and telemetry",
                           &CLASS_MODULE_UDPBROADCAST::s_terminalCmd,
                           kUdpCommands, kUdpHelp, true);
}

void CLASS_MODULE_UDPBROADCAST::s_terminalCmd() {
    char* sub = term.getNext();
    if (sub == nullptr || strcmp(sub, "help") == 0) {
        TerminalPrintText(kUdpHelp);
        return;
    }

    if (strcmp(sub, "show") == 0)    { cmdShow();   return; }
    if (strcmp(sub, "status") == 0)  { cmdStatus(); return; }
    if (strcmp(sub, "test") == 0)    { cmdTest();   return; }
    if (strcmp(sub, "send") == 0)    { cmdSend();   return; }
    if (strcmp(sub, "set") == 0)     { cmdSet();    return; }
    if (strcmp(sub, "apply") == 0)   { cmdApply();  return; }
    if (strcmp(sub, "save") == 0)    { cmdSave();   return; }
    if (strcmp(sub, "stop") == 0)    { cmdStop();   return; }

    UDP_LOG("Unknown udp subcommand '%s'. Type 'udp help'.\r\n", sub);
}
