# Profile 与裁剪

## 1. 基本规则

Profile 是经过验证的能力组合。

默认 profile：

```cpp
ESP32BASE_PROFILE_CORE
```

底层宏：

- `ESP32BASE_ENABLE_LOG`
- `ESP32BASE_ENABLE_CONFIG`
- `ESP32BASE_ENABLE_SYSTEM`
- `ESP32BASE_ENABLE_BUS`
- `ESP32BASE_ENABLE_WATCHDOG`
- `ESP32BASE_ENABLE_SLEEP`
- `ESP32BASE_ENABLE_FS`
- `ESP32BASE_ENABLE_FILELOG`
- `ESP32BASE_ENABLE_HEALTH`
- `ESP32BASE_ENABLE_WIFI`
- `ESP32BASE_ENABLE_DNS`
- `ESP32BASE_ENABLE_NTP`
- `ESP32BASE_ENABLE_MDNS`
- `ESP32BASE_ENABLE_WEB`
- `ESP32BASE_ENABLE_OTA`
- `ESP32BASE_ENABLE_WEB_OTA`

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
- `OTA` 需要 `WIFI` + `WEB`。第一版 OTA 是 Web OTA，不提供脱离 Web 的独立 OTA 传输入口。
- `WEB_OTA` 需要 `WEB` + `OTA`。
- `DNS` 需要 `WIFI`。
- `NTP` 需要 `WIFI`。
- `MDNS` 需要 `WIFI`。
- `FILELOG` 需要 `FS`。

软依赖：

- `WIFI` 可不依赖 Bus。
- `WEB` 可不依赖 Bus。
- `OTA` 可不依赖 Bus。
- `HEALTH` 可不依赖 Bus。

未启用 Bus 时：

- 模块不发布事件。
- 查询 API 仍可用。

启用 FS 的 profile 默认启用 FileLog；用户仍可显式关闭 `ESP32BASE_ENABLE_FILELOG`。

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

适合：

- 完整本地管理。
- Web OTA。
- 诊断压测。
- 设备长期维护。

## 12. 裁剪验收

必须通过 map 文件验证：

- CORE 无 WiFi / WebServer / Update / LittleFS 符号。
- NET 无 WebServer / Update 符号。
- WEB 无 Update 符号。
- 关闭 Bus / Fs / Health 时无对应静态对象。

更完整的裁剪验收表和资源记录规则见 [内存与容量预算](06_memory_budget.md)。
