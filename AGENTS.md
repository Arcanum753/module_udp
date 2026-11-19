# module_udp — UDP broadcast

> **Опциональный модуль.** Подключается через `src_filter` + `build_flags`.
> **Работоспособен только в составе сборки, содержащей ядро** (см. `../../TRS.md` §2.1).

UDP-обнаружение устройства в локальной сети и телеметрия: широковещательные и
одноадресные посылки, ответ на broadcast-запросы по ключевому слову, отслеживание
доступности сервера-получателя с фоллбэком на broadcast.

- **Репозиторий:** https://github.com/Arcanum753/module_udp
- **Папка:** `src/module_udp/`
- **Флаг активации:** `-D MODULE_UDP` (в env встречается и `-D MODULE_UDP=1`)
- **Registry:** `object=module_udp`, `define=MODULE_UDP`, `web=1`, `loop=0` (без `namespace`/`res`/`prio`)
- **Только для платформы:** обе (ESP8266/ESP32)
- **Зависит от модулей:** —
- **Зависит от ядра:** `core_web`, `core_sys`, `core_json`, `core_wifi` (вызывает `startSocket()` при подключении), `core_terminal`

## Назначение

Обеспечивает:
- UDP-широковещание для обнаружения устройств в локальной сети;
- настраиваемые порты приёма и передачи;
- ключевое слово для идентификации устройств;
- автоматический ответ на broadcast-запросы.

> [!WARNING]
> **Данный модуль находится в стадии активной разработки. API и функционал могут изменяться. Подробное руководство пользователя будет добавлено по мере стабилизации.**

## Функциональные требования

- FR-UDP-1: Конфиг `/config_udp.json`: `udpPortTx`, `udpPortRx`, `udpTimeOut`, `udpkeyword`, `udpPowerOn`, `udpResponse`, `udpMissLimit`.
- FR-UDP-2: Отправка broadcast (`broadcastSend`) и unicast (`unicastSend`), тест (`broadcastTest`), останов (`stop`).
- FR-UDP-3: Отслеживание сервера (`serverOnlineGet`, `serverIpGet`, `lastContactMsGet`) с фоллбэком при пропуске лимита.
- FR-UDP-4: Порт по умолчанию `UDP_PORT=40001`.
- FR-UDP-5: `begin(ctx)` — штатный вызов из registry: загрузка конфига и регистрация
  терминала; сокет поднимается отдельным `startSocket()` из `core_wifi` при подключении к
  сети; `stop()` — при потере соединения.
- FR-UDP-6: `applyConfig()` — общий путь применения конфига к сокету (`stop()` +
  `startSocket()`); вызывается из терминала (`udp apply`) и из веб-сохранения; при смене
  `udpPortRx` сокет переоткрывается сразу, без перезагрузки.
- FR-UDP-7: Терминальная команда `udp`: `show/status/send/test/set/apply/save/stop`;
  работа с конфигом доступна всегда, сетевые действия — только при активном подключении.

## Веб-интерфейс

Маршруты: `POST /udp.html`, `GET /udp/info`, `/udp/test`, `GET /udp/status`, `/udp/ver`.
Страница `udp.html` (пункт меню — `web/_menu.html`) доступна только при подключении модуля.
После сохранения формы вызывается `save_configUDP()` + `applyConfig()` (сокет
переоткрывается на новых портах при наличии сети).

## Конфигурация

`/config_udp.json` — настройки модуля (порты, ключевое слово, таймаут). Поля: см. FR-UDP-1.

## Терминальные команды

Реализация — `module_udp_terminal.cpp`; `terminalRegister()` вызывается из `begin()`.
Компонент в дескрипторе — `module_udp` (по нему `sys stat` ставит флаг `term`),
команда верхнего уровня — `udp`, `editable = true`. Справка — `udp help`.

| Подкоманда | Сеть | Назначение |
|---|---|---|
| `udp show` | нет | конфиг, состояние сокета, режим и статус сервера; пометка «not applied» |
| `udp status` | нет | состояние связи с сервером (online/IP/last contact/miss) |
| `udp test` | да | broadcast JSON-телеметрии (`jsonGet()`) на `udpPortTx` |
| `udp send <text>` | да | broadcast произвольного текста, 1..1023 байт |
| `udp set <field> <value>` | нет | только RAM: `porttx`, `portrx`, `timeout`, `keyword`, `poweron`, `response`, `misslimit` |
| `udp apply` | да | переоткрыть сокет с текущим конфигом |
| `udp save` | нет | сохранить `/config_udp.json` |
| `udp stop` | нет | закрыть сокет до следующего подключения/ребута |
| `udp help` | — | справка |

Правила:

- `porttx`/`portrx` — 1..65535 и не равны друг другу (`broadcastSend`/`unicastSend`
  молча отказывают при `port == udpPortRx`);
- `timeout` — 0..60 мин (0 — выключено); `misslimit` — 1..10;
- `keyword` — 1..63 printable-символов без пробелов, кавычек и `\` (вставляется в JSON
  телеметрии без экранирования; приёмный буфер — 64 байта);
- `set` меняет только RAM и ставит флаг «не применено»; `apply` переоткрывает сокет
  (нужна сеть); `save` пишет конфиг (без сети);
- `send`/`test` при неподнятом сокете печатают `UDP not started (no Wi-Fi connection).`,
  а при активном Wi-Fi — `UDP socket stopped. Run 'udp apply'.`;
- `stop` временный: следующий `core_wifi` connect снова поднимет сокет;
- строки терминала на ESP8266 — во flash (`PROGMEM`/`PSTR`), иначе не хватает DRAM.

## Слоистая структура

Из `../../LAYERS.md`: `module_udp` — UDP broadcast/unicast, связь с сервером.
Слои выделены: `module_udp_types.h` (define'ы и `strUdpConfig`), `module_udp.cpp`
(шаблонный блок: begin/web/конфиг/версии), `module_udp_terminal.cpp` (команда `udp`),
`module_udp_engine.cpp` + `module_udp_engine.h` (сокет, startSocket/applyConfig,
исполнительная логика).

## Критерии приёмки

- AC-14: UDP: broadcast-обнаружение и ответ на ключевое слово, статус сервера, фоллбэк;
  `startSocket()` вызывается при подключении Wi-Fi, `begin(ctx)` — из registry; `udp apply`
  и веб-сохранение переоткрывают сокет на новых портах.

## Ссылки

- Ядро и конвенции: `../../TRS.md`
- Слоистая структура: `../../LAYERS.md`
- Общие утилиты: `../../TRS.md` §3.1.12 (`common/`)
- Сборка: `../../BUILD.md`
- Реестр компонентов: `../../INVENTORY.md`

> Если модуль читается вне дерева ядра (standalone), корневые документы доступны в
> репозитории ядра avr-fota.
