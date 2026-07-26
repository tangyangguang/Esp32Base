# Profile 与裁剪

## 1. 基本规则

Profile 是经过验证的能力组合。

默认 profile：

```cpp
ESP32BASE_PROFILE_CORE
```

仓库示例默认面向 ESP32 4MB Flash，并使用 `partitions/esp32-4mb-ota-balanced.csv`。应用项目强烈推荐直接选择 `partitions/` 中已有分区表，不要在业务项目里重新设计分区布局；尤其是 FULL/Web OTA profile，固件必须小于下一 OTA app slot。classic ESP32 4MB 与 ESP32-C3 4MB 推荐分区表都保留两个 1.5MB OTA app slot，剩余空间作为 LittleFS 数据分区，并保持 `app0=0x10000`，不需要额外设置上传偏移。ESP32-S3、ESP32-C3 或 8MB 板型应选择对应 env 或匹配的分区表。`examples/basic` 提供 classic ESP32、ESP32-S3 8MB 和 ESP32-C3 4MB env；`full_demo`、`web_logs_ota` 和 `net_runtime` 也提供 ESP32-S3 8MB env。

底层宏：

- `ESP32BASE_ENABLE_LOG`
- `ESP32BASE_ENABLE_CONFIG`
- `ESP32BASE_ENABLE_SYSTEM`
- `ESP32BASE_ENABLE_BUS`
- `ESP32BASE_ENABLE_WATCHDOG`
- `ESP32BASE_ENABLE_SLEEP`
- `ESP32BASE_ENABLE_FS`
- `ESP32BASE_ENABLE_FILELOG`
- `ESP32BASE_ENABLE_RECORD_STORE`
- `ESP32BASE_ENABLE_APP_EVENTS`
- `ESP32BASE_ENABLE_APP_EVENT_CONDITIONS`
- `ESP32BASE_ENABLE_RS485_PORT`
- `ESP32BASE_ENABLE_HEALTH`
- `ESP32BASE_ENABLE_WIFI`
- `ESP32BASE_ENABLE_WIFI_RECOVERY_BUTTON`
- `ESP32BASE_ENABLE_DNS`
- `ESP32BASE_ENABLE_NTP`
- `ESP32BASE_ENABLE_TIME`
- `ESP32BASE_ENABLE_RTC`
- `ESP32BASE_ENABLE_MDNS`
- `ESP32BASE_ENABLE_WEB`
- `ESP32BASE_ENABLE_APP_CONFIG`
- `ESP32BASE_ENABLE_OTA`
- `ESP32BASE_ENABLE_WEB_OTA`
- `ESP32BASE_ENABLE_ARDUINO_OTA`

## 2. 展开契约

`Esp32BaseProfile.h` 必须按以下顺序处理：

1. 读取用户选择的 profile。
2. 用 `#ifndef` 展开 profile 默认能力。
3. 保留用户显式 `ESP32BASE_ENABLE_*` 覆盖。
4. 在所有展开完成后做依赖检查。

示例：

```cpp
#if ESP32BASE_PROFILE == ESP32BASE_PROFILE_NET
  #ifndef ESP32BASE_ENABLE_WIFI
    #define ESP32BASE_ENABLE_WIFI 1
  #endif
  #ifndef ESP32BASE_ENABLE_NTP
    #define ESP32BASE_ENABLE_NTP 1
  #endif
#endif

#if ESP32BASE_ENABLE_WEB && !ESP32BASE_ENABLE_WIFI
  #error "ESP32BASE_ENABLE_WEB requires ESP32BASE_ENABLE_WIFI"
#endif
```

Profile 默认值不能覆盖用户显式 `-D`。

## 3. 依赖规则

硬依赖：

- `WEB` 需要 `WIFI`。
- `OTA` 需要 `WIFI` + `WEB`。
- `WEB_OTA` 需要 `WEB` + `OTA`。
- `ARDUINO_OTA` 需要 `OTA`，默认值跟随 `OTA`；启用 OTA 的 profile 默认同时支持 Web OTA 和 espota。
- `DNS` 需要 `WIFI`。
- `NTP` 需要 `WIFI`。
- `NTP`、`RTC` 和 `RECORD_STORE` 需要 `TIME`；`TIME` 在启用 NTP、RTC、RecordStore、App Events 或 Web 时默认启用。
- `MQTT` 需要 `WIFI` 和 `TIME`，默认关闭，不随任何 profile 自动开启；MQTTS 运行时还要求可信 RTC/NTP 时间。
- `RTC` 默认关闭，通过 `ESP32BASE_ENABLE_RTC=1` 显式启用。
- `MDNS` 需要 `WIFI`。
- `FILELOG` 需要 `FS`。
- `APP_EVENTS` 需要 `RECORD_STORE`（因此也需要 `FS` 和 `TIME`），默认关闭，不随任何 profile 自动开启。
- `APP_EVENT_CONDITIONS` 需要 `APP_EVENTS`，默认值跟随 `APP_EVENTS`；显式设为0时保留离散事件记录，裁掉条件状态机和专属NVS状态。
- `RECORD_STORE` 需要 `FS` 和 `TIME`，默认关闭；启用 App Events 时自动启用 RecordStore。
- `APP_CONFIG` 需要 `WEB`，默认关闭，不随任何 profile 自动开启。
- `RS485_PORT` 无 Esp32Base 内部硬依赖，默认关闭，不随任何 profile 自动开启；业务通过 `ESP32BASE_ENABLE_RS485_PORT=1` 显式启用，并自行选择 `HardwareSerial`、RX/TX/DE 引脚和串口参数。

软依赖：

- `WIFI` 可不依赖 Bus。
- `WEB` 可不依赖 Bus。
- `OTA` 可不依赖 Bus。
- `HEALTH` 可不依赖 Bus。

WiFi恢复按键能力默认跟随WiFi编译，不新增Profile，可用`ESP32BASE_ENABLE_WIFI_RECOVERY_BUTTON=0`完整裁掉。构建默认由`ESP32BASE_WIFI_RECOVERY_BUTTON_DEFAULT_ENABLED`、`ESP32BASE_WIFI_RECOVERY_BUTTON_GPIO`和`ESP32BASE_WIFI_RECOVERY_BUTTON_HOLD_MS`控制；ESP32 / ESP32-S3默认GPIO0，ESP32-C3默认GPIO9，默认启用并长按10000ms。运行时保存到独立`eb_wifi_rcv` namespace，可在System页覆盖或关闭；运行时关闭或WiFi未启用时不轮询GPIO。

未启用 Bus 时：

- 模块不发布事件。
- 查询 API 仍可用。

启用 FS 的 profile 默认启用系统诊断日志。底层实现/API 名称仍为 FileLog（`Esp32BaseFileLog`）；用户仍可显式关闭 `ESP32BASE_ENABLE_FILELOG`。

通用固定业务记录通过 `ESP32BASE_ENABLE_RECORD_STORE=1` 显式启用。应用为每个记录类型在运行时提供固定负载大小、存储版本、最大逻辑Store字节预算和可选LittleFS最低剩余空间，不新增Profile。

App Events通过 `ESP32BASE_ENABLE_APP_EVENTS=1` 显式启用，并复用RecordStore。`ESP32BASE_APP_EVENT_STORE_MAX_BYTES` 默认 `100 * 1024` 字节；App Events在 `Esp32Base::begin()` 内优先创建，不提供单独最低剩余空间配置。`ESP32BASE_ENABLE_APP_EVENT_CONDITIONS` 默认跟随App Events启用，不新增Profile，也不提供容量宏；固定1～32号条件使用一个32位NVS位图，ID含义属于保留该NVS状态的固件版本共同遵守的持久化schema。

RS485 半双工基础串口通过 `ESP32BASE_ENABLE_RS485_PORT=1` 显式启用。它只负责 `HardwareSerial` 初始化、DE 方向脚切换、发送后 `flush()` 等待和轮询读取，不创建后台任务，不分配协议缓冲，不解析帧，也不内置 Modbus/RTU、CRC、重试或业务命令。

MQTT Client 通过 `ESP32BASE_ENABLE_MQTT=1` 显式启用，不新增 Profile。默认只允许 MQTTS；受控局域网/测试固件必须同时设置 `ESP32BASE_MQTT_ALLOW_PLAINTEXT=1` 并在连接配置中显式选择明文。MQTT 开启后固定引入 ESP-MQTT task、收发缓冲和有限邮箱；MQTT 关闭时不得链接 `Esp32BaseMqtt`、`esp_mqtt_client_*` 或 `mqtt_task` 符号。

外部 RTC 是可选时间源，不随任何 profile 自动启用。应用固件必须在构建期二选一配置驱动，不做运行时自动识别：

```ini
build_flags =
  -D ESP32BASE_ENABLE_RTC=1
  -D ESP32BASE_RTC_DRIVER=ESP32BASE_RTC_DRIVER_DS3231
```

可选驱动为 `ESP32BASE_RTC_DRIVER_DS3231` 和 `ESP32BASE_RTC_DRIVER_PCF8563`。默认 `ESP32BASE_RTC_I2C_ADDR=0` 表示使用芯片默认地址；也可显式设置 7-bit I2C 地址。`ESP32BASE_RTC_AUTO_WIRE_BEGIN=0` 时，应用负责在 `Esp32Base::begin()` 前初始化 `Wire` 或自定义 `TwoWire` 并调用 `Esp32BaseRtc::configure(...)`；设置为 `1` 时基础库只为 RTC 调用一次 `Wire.begin()`，可配合 `ESP32BASE_RTC_SDA`、`ESP32BASE_RTC_SCL`、`ESP32BASE_RTC_I2C_CLOCK_HZ` 使用。

应用持久化参数配置页通过 `ESP32BASE_ENABLE_APP_CONFIG=1` 显式启用，依赖 Web。业务必须在 `Esp32Base::begin()` 前注册分组和字段；保存前 veto、字段校验和保存后回调语义见 [API 契约](03_api.md) 与 [Web 与配网](04_web.md)。

## 4. 最终 Profile 表

| Profile | Core | Bus | Runtime | WiFi | DNS | NTP | mDNS | Web | OTA | 用途 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| CORE | yes | no | no | no | no | no | no | no | no | 极简配置和诊断 |
| RUNTIME | yes | yes | yes | no | no | no | no | no | no | 离线本地设备 |
| NET | yes | no, 可显式开启 | no | yes | yes | yes | yes | no | no | 纯联网设备 |
| NET_RUNTIME | yes | yes | yes | yes | yes | yes | yes | no | no | 量产联网节点 |
| WEB | yes | yes, 可显式关闭 | no | yes | yes | yes | yes | yes | no | Web 配置和状态 |
| WEB_RUNTIME | yes | yes | yes | yes | yes | yes | yes | yes | no | Web + 本地可靠运行 |
| FULL | yes | yes | yes | yes | yes | yes | yes | yes | yes | 完整管理和 OTA |

Profile 数量固定为 7 个，不再扩张。

## 5. ESP32BASE_PROFILE_CORE

启用：

- Log
- Config
- System

目标：

- 不链接 WiFi。
- 不链接 WebServer。
- 不链接 Update。
- 不链接 LittleFS。
- 不启动 AP。
- 不挂载文件系统。

适合：

- 极简控制器。
- 只需要 NVS 和系统诊断的设备。

## 6. ESP32BASE_PROFILE_RUNTIME

启用：

- CORE
- Bus
- Watchdog
- Sleep
- Fs
- Health

适合：

- 离线设备。
- 需要 LittleFS、Watchdog、Sleep 的本地设备。

## 7. ESP32BASE_PROFILE_NET

启用：

- CORE
- WiFi
- DNS
- NTP
- mDNS

默认不启用 Bus。

适合：

- 纯联网设备。
- 显式启用基础库 MQTT Client，或由业务自行使用 HTTP Client / 私有协议。

## 8. ESP32BASE_PROFILE_NET_RUNTIME

启用：

- NET
- Bus
- Watchdog
- Sleep
- Fs
- Health

适合：

- 量产联网节点。
- WiFi + 本地文件 + 看门狗。
- 不需要 Web / OTA 的传感器或执行器。

这是推荐生产联网节点 profile。

## 9. ESP32BASE_PROFILE_WEB

启用：

- NET
- Bus
- Web
- Web status handler group
- Web WiFi handler group

默认不启用：

- Fs
- Watchdog
- Sleep
- OTA

适合：

- 需要 Web 配置和状态页面，但不需要本地文件和 OTA 的设备。

Bus 默认启用，但可显式关闭。

## 10. ESP32BASE_PROFILE_WEB_RUNTIME

启用：

- WEB
- Watchdog
- Sleep
- Fs
- Health

不启用：

- OTA

适合：

- Web 配置 + 本地可靠运行。
- 不通过本库做 OTA 的量产设备。

## 11. ESP32BASE_PROFILE_FULL

启用：

- WEB_RUNTIME
- OTA
- Web OTA route hooks
- ArduinoOTA / espota command-line OTA

适合：

- 完整本地管理。
- Web OTA。
- PlatformIO `espota` 命令行 OTA。
- 诊断压测。
- 设备长期维护。

## 12. 裁剪验收

必须通过 map 文件验证：

- CORE 无 WiFi / WebServer / Update / LittleFS 符号。
- NET 无 WebServer / Update 符号。
- WEB 无 Update 符号。
- 关闭 Bus / Fs / Health 时无对应静态对象。

更完整的裁剪验收和容量边界见 [内存与容量预算](06_memory_budget.md)。
