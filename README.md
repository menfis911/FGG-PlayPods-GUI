<p align="center">
  <img src="sce_sys/icon0.png" alt="AudioBridge — GUI" width="180">
</p>

<h1 align="center">AudioBridge — GUI</h1>

<p align="center">
  <strong>Web GUI для передачи всего звука PS5 на обычные Bluetooth-наушники.</strong><br>
  Без USB-донгла, через Bluetooth-контроллер консоли, с сохранением беспроводного DualSense.
</p>

| | |
|---|---|
| **Основа** | [FGG-PlayPods](https://github.com/FGGstore/FGG-PlayPods) от [FathiGhanem](https://github.com/FathiGhanem) |
| **Идея AudioBridge** | заменить автоматическое подключение к первому устройству на управляемое приложение с Web GUI |
| **Как работает** | native backend захватывает звук PS5 и передаёт его через SBC/A2DP; Web GUI управляет backend через JSON API |
| **Что добавлено** | scan, список устройств, RSSI, выбор по MAC, pairing, connect/disconnect, saved devices, статус streaming и websrv-плитка |
| **Версия** | `0.2.0` |

AudioBridge — самостоятельное продолжение и производная работа на базе FGG-PlayPods. Проверенная Bluetooth/audio-реализация сохранена, а приложение получило новую архитектуру: асинхронный backend, неблокирующий HTTP API и интерфейс для телевизора вместо собственного полноэкранного VideoOut GUI.

## Происхождение проекта

AudioBridge вырос из [FGG-PlayPods](https://github.com/FGGstore/FGG-PlayPods), созданного [FathiGhanem](https://github.com/FathiGhanem) и опубликованного FGG Store. Автор исходного проекта реализовал наиболее сложную часть: исследовал аудиозахват PS5 и работу её Bluetooth-контроллера, написал A2DP source, SSP pairing, L2CAP, SDP, AVDTP и интеграцию SBC.

На этой базе AudioBridge добавляет собственную архитектуру приложения:

- асинхронный backend с управляемым состоянием;
- JSON HTTP API;
- выбор конкретного Bluetooth-устройства вместо автоматического подключения к первому найденному;
- Web GUI и интеграцию с `ps5-payload-dev/websrv`;
- список найденных и сохранённых устройств;
- неблокирующие scan, connect и disconnect;
- TV-friendly навигацию с хорошо видимым focus;
- отдельную систему сборки и websrv release-пакет.

Отдельная благодарность **FathiGhanem** за оригинальный порт и инженерную работу, на которой основан AudioBridge. Также спасибо проекту [ps5-payload-dev](https://github.com/ps5-payload-dev) за SDK и websrv, а BlueZ — за SBC codec. История происхождения и лицензии сохранены намеренно.

## Архитектура

```text
websrv Homebrew tile
        │ запускает eboot.elf
        │ открывает http://<hostname-websrv>:18195/
        ▼
HTML / CSS / JavaScript ── HTTP JSON API ── native backend worker
                                                 │
                          capture ── SBC ── A2DP ─┴─ Bluetooth headset
```

- `src/backend.c` — worker и неблокирующее состояние Bluetooth/A2DP;
- `src/http_server.c` — HTTP-сервер и JSON API на `0.0.0.0:18195`;
- `web/` — встроенный TV-friendly интерфейс без внешних зависимостей;
- `src/bt.c`, `src/a2dp.c`, `src/capture.c`, `src/hci.c`, `src/sdp.c` — native Bluetooth/audio-реализация;
- `homebrew.js` — плитка и запуск приложения из websrv.

Старые `gui.c`, `video.c`, `pad.c` и `ps5_tilemap.inc` не входят в сборку. AudioBridge не открывает VideoOut и не перехватывает экран консоли.

## Web API

Backend слушает все IPv4-интерфейсы на порту `18195`. Длительные операции выполняются worker-потоком, поэтому HTTP и Web GUI не блокируются во время scan, pairing, connect или streaming.

| Метод | Endpoint | Назначение |
|---|---|---|
| `GET` | `/api/status` | состояние controller, scan, connection и stream |
| `GET` | `/api/devices` | найденные устройства: name, MAC, RSSI и state |
| `POST` | `/api/scan` | поставить discovery в очередь |
| `POST` | `/api/connect` | подключить устройство; JSON: `{"mac":"AA:BB:CC:DD:EE:FF"}` |
| `POST` | `/api/disconnect` | остановить stream и отключить устройство |
| `GET` | `/api/saved` | устройство с сохранённым link key |

Команды принимаются с `202 Accepted`. Если backend занят другой несовместимой операцией, API возвращает `409 Conflict`.

## Интеграция с websrv

Актуальный `websrv` ищет Homebrew в `/data/homebrew`, `/mnt/usb*/homebrew` и `/mnt/ext*/homebrew`. Поддерживаемый формат — папка приложения с `eboot.elf`, `sce_sys/icon0.png` и опциональным `homebrew.js`. Контракт описан в [официальном README websrv](https://github.com/ps5-payload-dev/websrv#installing-homebrew).

Плитка запускает `eboot.elf`, ожидает `/api/status` и открывает Web GUI. Hostname берётся из текущей страницы websrv, поэтому один пакет работает и в PS5 webview, и при открытии websrv по IP консоли с компьютера. Hardcoded IP не используется.

Структура release-пакета:

```text
AudioBridge-GUI/
├── eboot.elf
├── homebrew.js
└── sce_sys/
    └── icon0.png
```

Скопируйте папку `AudioBridge-GUI` в `/data/homebrew/`, запустите websrv и откройте Homebrew Launcher. В интерфейсе появится плитка **AudioBridge — GUI**.

## Данные и обновление с FGG-PlayPods-GUI

Чтобы обновление не удаляло pairing и не требовало повторного сопряжения, AudioBridge сохраняет совместимый каталог предыдущей GUI-версии:

```text
/data/fgg-playpods-gui/paired.key       # бинарный Bluetooth link key
/data/fgg-playpods-gui/saved-device.txt # имя и MAC сохранённого устройства
/data/fgg-playpods-gui/gui-playpods.log # лог текущего запуска
```

Это внутренний стабильный путь данных, а не отображаемое название продукта. Формат `paired.key` не изменён. Переименование GitHub-репозитория или websrv-папки не влияет на сохранённые Bluetooth-ключи.

## Ограничения

- одно A2DP-устройство одновременно;
- Classic Bluetooth Audio / SBC; LE Audio-only устройства не поддерживаются;
- передаётся звук, микрофон не поддерживается;
- громкость регулируется на гарнитуре;
- Bluetooth SBC добавляет задержку;
- payload использует Bluetooth-контроллер совместно с системным драйвером и не отключает DualSense.

## Сборка и проверки

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make check
make
```

Результат сборки — `audiobridge-gui.elf`. Web assets встраиваются в ELF, поэтому отдельная папка `web/` на PS5 не требуется.

Для локального просмотра запускайте страницу через HTTP так же, как её обслуживает backend на PS5:

```sh
make preview
# открыть http://127.0.0.1:18196/
```

При прямом открытии `web/index.html` через `file://` некоторые встроенные браузеры блокируют локальные CSS/JS subresources. Это ограничение preview-режима, а не PS5-пакета.

Для прямой диагностики отправьте `audiobridge-gui.elf` в `elfldr` и откройте `http://<PS5-IP>:18195/` с другого устройства.

## Release

Теги `v*` собираются GitHub Actions. Workflow проверяет совпадение тега с `VERSION`, собирает ELF и `AudioBridge-GUI-websrv-<version>.zip`, а затем создаёт только новый GitHub Release. Существующие releases никогда не перезаписываются.

## Лицензия

Проект распространяется под GPL-3.0. Vendored SBC codec в `third_party/sbc` сохраняет LGPL-2.1-or-later; подробности находятся в `third_party/sbc/VENDORED.md`.
