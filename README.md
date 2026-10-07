ПОКА В РАЗРАБОТКЕ, Альфа.
<h1 align="center">FGG-PlayPods-GUI</h1>

<p align="center">
  <b>Форк FGG-PlayPods с графическим интерфейсом для PS5.</b><br>
  Оригинальный проект: <a href="https://github.com/FGGstore/FGG-PlayPods">FGGstore/FGG-PlayPods</a><br>
  Цель форка — превратить текущий автоматический payload в полноценное приложение<br>
  с поиском Bluetooth-устройств, выбором устройства, ручным подключением,<br>
  сохранёнными устройствами и подробным отображением состояния и логов.
</p>

> ⚠️ **Статус форка:** сейчас это исходный код оригинального FGG-PlayPods без изменений в логике Bluetooth/audio. GUI и выбор устройства пока находятся в плане разработки. На этом этапе изменена только документация форка.

<p align="center">
  <a href="https://github.com/FGGstore/FGG-PlayPods/actions/workflows/ci.yml"><img src="https://github.com/FGGstore/FGG-PlayPods/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPL v3"></a>
  <img src="https://img.shields.io/badge/PS5-firmware%2013.60-003791" alt="Tested on firmware 13.60">
</p>

---

## Что это

Этот репозиторий — форк **FGG-PlayPods**.

Оригинальный проект позволяет выводить игровой и системный звук PS5 на обычные Bluetooth-наушники через собственный Bluetooth-контроллер консоли, без USB-донгла. При этом DualSense продолжает работать по беспроводной связи.

В оригинальной версии устройство выбирается автоматически: payload сканирует Bluetooth и подключается к первому найденному подходящему аудиоустройству. Именно эту часть планируется переработать в данном форке.

Планируемая схема:

```
PS5 audio ──capture──► FGG-PlayPods-GUI ──A2DP/SBC──► выбранное Bluetooth-устройство
                              │
                              ├── поиск устройств
                              ├── выбор устройства
                              ├── Pair / Connect
                              ├── сохранённые устройства
                              └── состояние и логи
```

### Возможности оригинального FGG-PlayPods

| Возможность | Описание |
|---|---|
| **Что передаётся** | игры, меню, уведомления — весь звук консоли |
| **Устройства** | Bluetooth-наушники, гарнитуры и earbuds с A2DP / SBC |
| **Аудио** | 48 кГц stereo, SBC высокого качества (bitpool 53) |
| **DualSense** | остаётся беспроводным и полностью рабочим |
| **Установка** | ничего не патчится и не устанавливается за пределами собственной папки |

## Планируемые изменения форка

Главная задача — заменить автоматический выбор первого найденного устройства на управляемый графический интерфейс.

### 1. Поиск Bluetooth-устройств

GUI должен уметь запускать сканирование и отображать найденные устройства:

- имя;
- MAC-адрес;
- Bluetooth Class;
- RSSI, если контроллер его предоставляет;
- статус устройства;
- признак сохранённого/сопряжённого устройства.

### 2. Выбор устройства

Пользователь выбирает конкретное устройство из списка и запускает:

**Подключить / Pair**

Payload больше не должен автоматически брать первое найденное устройство.

### 3. Сохранённые устройства

После успешного pairing планируется сохранять:

- имя;
- MAC-адрес;
- link key;
- дополнительные данные, необходимые для повторного подключения.

Это позволит выбирать устройство без нового pairing при каждом запуске.

### 4. Повторное подключение

Для ранее сопряжённого устройства:

```
Запуск → список сохранённых устройств → выбрать → Connect
```

Также можно будет предусмотреть автоматическое подключение к последнему выбранному устройству.

### 5. Логи

В GUI планируется отдельный экран с событиями Bluetooth/audio.

Например:

```
[12:49:15] Найдено устройство: JBL ...
[12:49:15] Подключение...
[12:49:16] Аутентификация...
[12:49:16] Ошибка: authentication failed 0x05
```

При этом существующий файл:

```
/data/fgg-playpods/playpods.log
```

будет сохранён.

### 6. Статус подключения

GUI должен явно показывать:

- Bluetooth controller;
- поиск;
- подключение;
- pairing;
- authentication;
- A2DP;
- streaming;
- отключение/ошибку.

---

## Оригинальная реализация

Оригинальный FGG-PlayPods решает несколько сложных задач непосредственно внутри payload.

### Захват аудио

Используется внутренний audio capture PS5 таким образом, чтобы получать полный микс, включая игровой звук и системные звуки.

Аудиоформат:

- 48 кГц;
- stereo;
- 32-bit float внутри capture;
- 1024 frames на запись.

### Bluetooth

PS5 имеет Bluetooth, но штатно не предоставляет обычный Bluetooth Audio.

FGG-PlayPods реализует собственный A2DP source внутри payload:

- SSP pairing;
- L2CAP;
- SDP;
- AVDTP;
- SBC;
- RTP audio.

### Совместное использование Bluetooth-контроллера

Bluetooth-чип консоли предоставляет два HCI-контроллера.

Системный драйвер использует один из них для DualSense, а FGG-PlayPods работает рядом с системным драйвером через другой интерфейс, не отключая системный Bluetooth-драйвер.

Это принципиально важно для будущего GUI: при добавлении сканирования и выбора устройства нельзя нарушить текущую схему совместного использования контроллера.

### Поток данных

```
PS5 audio capture
       ↓
48 kHz stereo
       ↓
SBC encoder
       ↓
A2DP / RTP
       ↓
Bluetooth controller
       ↓
Bluetooth headset
```

## Структура проекта

| Файл | Назначение |
|---|---|
| `src/capture.c` | захват аудио PS5 |
| `src/hci.c` | USB-транспорт к Bluetooth-контроллеру |
| `src/bt.c` | pairing, Bluetooth link, L2CAP и flow control |
| `src/sdp.c` | SDP service record |
| `src/a2dp.c` | AVDTP, SBC encoding и аудиопоток |
| `src/log.c` | файловый лог и уведомления PS5 |
| `src/main.c` | запуск, lock и основная сессия |
| `third_party/sbc` | SBC codec |

## Установка FGG-PlayPods-GUI

### Payload Manager — основной способ

Форк сохраняет тот же способ установки, что и оригинальный проект:

1. Собрать `fgg-playpods-gui.elf`.
2. Скопировать **оба файла** через FTP:

```
fgg-playpods-gui.elf
fgg-playpods-gui.elf.json
```

в отдельную папку:

```
/data/pldmgr/payloads/fgg-playpods-gui/
```

3. Открыть **Payload Manager**.
4. Запустить **FGG-PlayPods-GUI**.

Отдельное имя и отдельная папка нужны, чтобы форк мог находиться рядом с оригинальным **FGG-PlayPods** без перезаписи его файлов и состояния. Payload Manager использует каталог `/data/pldmgr/payloads` для хранения payload-файлов. citeturn0search2

### Direct

Для прямого запуска сохраняется стандартный вариант:

```
fgg-playpods-gui.elf → порт 9021
```

То есть форк можно запускать и через Payload Manager, и напрямую через payload sender.

> **Важно:** оригинальный FGG-PlayPods и FGG-PlayPods-GUI можно хранить одновременно, но запускать их одновременно не следует — оба используют общий Bluetooth-контроллер PS5.

## Использование оригинальной версии

1. Перевести Bluetooth-гарнитуру в режим pairing.
2. Запустить payload.
3. Payload автоматически найдёт подходящее аудиоустройство.
4. Выполнит pairing и начнёт передавать звук.
5. При следующих запусках будет использовать сохранённый link key.

Для остановки оригинального payload достаточно выключить гарнитуру.

> В будущей версии GUI автоматический выбор первого найденного устройства будет заменён на выбор пользователем.

---

## Ограничения оригинальной версии

- **Одно устройство одновременно.**
- Для pairing другого устройства необходимо удалить:
  `/data/fgg-playpods/headset.key`.
- Передаётся только звук — микрофон не поддерживается.
- Громкость регулируется на самой гарнитуре.
- Bluetooth SBC добавляет задержку, поэтому решение не идеально для rhythm games.
- Только Classic Bluetooth Audio / A2DP. LE Audio-only устройства не поддерживаются.

## Логи

Каждый запуск записывается в:

```
/data/fgg-playpods/playpods.log
```

Получить файл можно по FTP.

Для диагностики pairing особенно важны сообщения:

```
found ...
connection complete ...
authentication complete ...
authentication failed ...
disconnected ...
chip vendor event ...
```

## Сборка

Требуется Linux или WSL и [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk):

```sh
sudo apt install clang lld llvm make unzip
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make
```

Сборка выполняется с:

```
-Wall -Wextra -Werror
```

Единственный сторонний код — SBC codec из [BlueZ](https://www.bluez.org/), размещённый в `third_party/sbc`.

## Лицензия

[GPL-3.0](LICENSE). Vendored SBC codec распространяется по LGPL-2.1-or-later.

## Credits

Built by **FGG STORE**.

Standing on the work of [ps5-payload-dev](https://github.com/ps5-payload-dev)
for the SDK and `ftpsrv`, of [BlueZ](https://www.bluez.org/) for the SBC codec,
and of [idlesauce/ps5-self-pager](https://github.com/idlesauce/ps5-self-pager),
which made reading the system software possible.

> Not affiliated with Sony Interactive Entertainment or Microsoft. For use with
> homebrew on consoles you own.
