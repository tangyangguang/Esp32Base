# Profile 与裁剪

## 1. 基本规则

Profile 是经过验证的能力组合。

默认 profile：

```cpp
ESP32BASE_PROFILE_CORE
```

仓库示例默认面向 ESP32 4MB Flash，并使用 `partitions/esp32-4mb-ota-balanced.csv`。ESP32-S3、ESP32-C3 或 8MB 板型应选择对应 env 或匹配的分区表；尤其是 FULL/Web OTA profile，固件必须小于下一 OTA app slot。`examples/basic` 提供 classic ESP32、ESP32-S3 8MB 和 ESP32-C3 4MB env；`full_demo`、`web_logs_ota` 和 `net_runtime` 也提供 ESP32-S3 8MB env。

底层宏：

- `ESP32BASE_ENABLE_LOG`
- `ESP32BASE_ENABLE_CONFIG`
- `ESP32BASE_ENABLE_SYSTEM`
- `ESP32BASE_ENABLE_BUS`
- `ESP32BASE_ENABLE_WATCHDOG`
- `ESP32BASE_ENABLE_SLEEP`
- `ESP32BASE_ENABLE_FS`
- `ESP32BASE_ENABLE_FILELOG`
- `ESP32BASE_ENABLE_APP_EVENTS`
- `ESP32BASE_ENABLE_HEALTH`
- `ESP32BASE_ENABLE_WIFI`
- `ESP32BASE_ENABLE_DNS`
- `ESP32BASE_ENABLE_NTP`
- `ESP32BASE_ENABLE_MDNS`
- `ESP32BASE_ENABLE_WEB`
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
#if defined(ESP32BASE_PROFILE_NET)
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
- `MDNS` 需要 `WIFI`。
- `FILELOG` 需要 `FS`。
- `APP_EVENTS` 需要 `FS`，默认关闭，不随任何 profile 自动开启。

软依赖：

- `WIFI` 可不依赖 Bus。
- `WEB` 可不依赖 Bus。
- `OTA` 可不依赖 Bus。
- `HEALTH` 可不依赖 Bus。

未启用 Bus 时：

- 模块不发布事件。
- 查询 API 仍可用。

启用 FS 的 profile 默认启用 FileLog；用户仍可显式关闭 `ESP32BASE_ENABLE_FILELOG`。

应用事件日志通过 `ESP32BASE_ENABLE_APP_EVENTS=1` 显式启用，默认容量 `ESP32BASE_APP_EVENT_LOG_CAPACITY=1024`，允许范围 `64..2048`。该能力使用 LittleFS 固定文件存储，适合结构化业务事件，不作为系统 FileLog 或调试日志。

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
- 业务层自己使用 MQTT / HTTP Client / 私有协议。

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

更完整的裁剪验收表和资源记录规则见 [内存与容量预算](06_memory_budget.md)。
