# FGG-PlayPods-GUI

Web-интерфейс для вывода звука PS5 на Bluetooth-наушники через A2DP/SBC.

Версия **0.2.0** заменяет собственный полноэкранный VideoOut GUI на локальный Web GUI. Проверенная Bluetooth/audio-реализация сохранена: HCI, discovery, SSP pairing, link key, L2CAP, SDP, AVDTP, SBC и захват системного звука работают в native backend.

## Архитектура

```text
websrv Homebrew tile
        │ запускает eboot.elf и открывает http://127.0.0.1:18195/
        ▼
HTML / CSS / JavaScript ── HTTP JSON API ── native backend worker
                                                 │
                          capture ── SBC ── A2DP ─┴─ Bluetooth headset
```

- `src/backend.c` — асинхронное состояние и worker Bluetooth/A2DP;
- `src/http_server.c` — локальный HTTP-сервер и JSON API;
- `web/` — TV-friendly интерфейс без внешних зависимостей;
- `src/bt.c`, `src/a2dp.c`, `src/capture.c`, `src/hci.c`, `src/sdp.c` — сохранённая native-реализация;
- `homebrew.js` — расширение плитки для `ps5-payload-dev/websrv`.

Старые `gui.c`, `video.c`, `pad.c` и `ps5_tilemap.inc` больше не входят в сборку. Payload не открывает VideoOut и не перехватывает экран.

## Web API

Backend слушает `0.0.0.0:18195`. Web GUI использует тот же origin.

| Метод | Endpoint | Назначение |
|---|---|---|
| `GET` | `/api/status` | состояние controller, scan, connection и stream |
| `GET` | `/api/devices` | найденные audio-устройства: name, MAC, RSSI, state |
| `POST` | `/api/scan` | поставить discovery в очередь |
| `POST` | `/api/connect` | подключить устройство; JSON: `{"mac":"AA:BB:CC:DD:EE:FF"}` |
| `POST` | `/api/disconnect` | остановить stream и отключить устройство |
| `GET` | `/api/saved` | устройство, для которого сохранён link key |

Команды возвращают `202 Accepted`, а длительная работа выполняется в Bluetooth worker. HTTP и UI не блокируются во время scan, pairing, connect или streaming. Если backend занят, API возвращает `409 Conflict`.

## Интеграция с websrv

Актуальный `websrv` ищет Homebrew в `/data/homebrew`, `/mnt/usb*/homebrew` и `/mnt/ext*/homebrew`. Минимальный поддерживаемый формат — папка приложения с `eboot.elf` и `sce_sys/icon0.png`; опциональный `homebrew.js` настраивает плитку и её действие. Это описано в [официальном README websrv](https://github.com/ps5-payload-dev/websrv#installing-homebrew), а контракт `main()` показан в [официальном demo/homebrew.js](https://github.com/ps5-payload-dev/websrv/blob/master/homebrew/demo/homebrew.js).

Ограничения websrv, учтённые в пакете:

- `homebrew.js::main()` должен быстро вернуть объект плитки (лимит websrv — 5 секунд);
- websrv запускает ELF, но не проксирует произвольный API приложения;
- порт `8080` уже занят самим websrv;
- у BigApp/Homebrew нет универсального API запуска через обычный POST.

Поэтому плитка запускает `eboot.elf`, ждёт доступности backend и переводит браузер на `http://127.0.0.1:18195/`. Backend сам обслуживает встроенные HTML/CSS/JS и API на отдельном порту. Собственный несовместимый манифест не используется.

Структура release-пакета:

```text
FGG-PlayPods-GUI/
├── eboot.elf
├── homebrew.js
└── sce_sys/
    └── icon0.png
```

Скопируйте папку `FGG-PlayPods-GUI` в `/data/homebrew/`, запустите `websrv`, затем откройте Homebrew Launcher. Появится отдельная плитка **FGG-PlayPods-GUI**.

## Состояние и совместимость

Данные сохраняются отдельно от оригинального FGG-PlayPods:

```text
/data/fgg-playpods-gui/paired.key       # существующий бинарный link key
/data/fgg-playpods-gui/saved-device.txt # отображаемое имя, без ключа
/data/fgg-playpods-gui/gui-playpods.log # лог текущего запуска
```

Формат `paired.key` не изменён. Для устройства, сопряжённого старой версией GUI, `/api/saved` покажет MAC из ключа и временное имя до следующего успешного подключения.

Ограничения Bluetooth остаются прежними:

- одно A2DP-устройство одновременно;
- Classic Bluetooth Audio / SBC; LE Audio-only не поддерживается;
- передаётся звук, микрофон не поддерживается;
- громкость регулируется на гарнитуре;
- payload использует Bluetooth-контроллер совместно с системным драйвером, не отключая DualSense.

## Сборка и проверки

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make check
make
```

`make check` проверяет Web GUI, обязательные API endpoints, контракт `homebrew.js`, metadata и синтаксис JavaScript. `make` встраивает Web assets в ELF, поэтому отдельные файлы `web/` на PS5 не требуются.

Для прямого диагностического запуска можно отправить `fgg-playpods-gui.elf` в `elfldr` и открыть `http://<PS5-IP>:18195/` с другого устройства.

## Release

Теги `v*` собираются GitHub Actions. Workflow проверяет, что тег совпадает с `VERSION`, собирает ELF и создаёт новый GitHub Release только для нового тега. Существующие releases не перезаписываются.

## License

GPL-3.0. SBC codec в `third_party/sbc` сохраняет собственную лицензию LGPL; подробности в `third_party/sbc/VENDORED.md`.
