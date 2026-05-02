# 诊断与实机压测

## 1. 启动诊断

启动后会输出模块状态：

```text
Esp32Base fw name=... version=... build=... hostname=...
BaseCfg ready=1 pending=0/8 reserved_ns=esp32base
BaseNet ready=1 state=... ssid=... ip=... rssi=... ntp=... mdns=...
BaseWeb ready=1 auth=1 routes=.../16
BaseRun ready=1 heap=... flash=... reset=... wake=... fs=... wdt=...
```

## 2. 必测路径

实机发布前至少测试：

- 首次启动无 WiFi 凭证，自动开启 `Esp32Base-Config` AP。
- 通过 Web WiFi 页面提交 SSID 和密码。
- STA 连接成功后 `/esp32base/api/status` 返回正确状态。
- `hostname.local` 可访问。
- NTP 同步后 `formatTime()` 能输出真实时间。
- `/esp32base/ota` 能完成固件上传并重启。
- LittleFS 写入、读取、删除正常。
- Watchdog 启用后正常循环不会复位。
- 故意阻塞主循环时 Watchdog 能复位并增加计数。
- deep sleep 前 config flush，唤醒后 reset / wake reason 正确。

## 3. 压测记录格式

建议记录：

| 项目 | 记录 |
| --- | --- |
| 板型 | ESP32 / ESP32-S3 / ESP32-C3 |
| Arduino Core | PlatformIO espressif32 版本 |
| 启动 free heap | |
| WiFi connected free heap | |
| Web request min heap | |
| OTA min heap | |
| LittleFS 操作 min heap | |
| 1 小时 min heap | |
| OTA 是否成功 | |
| Watchdog 是否成功复位 | |
| deep sleep wake reason | |

## 4. 当前验证状态

当前已完成 PlatformIO 编译验证：

- `examples/basic`
- `examples/wifi_network`
- `examples/wifi_web_ota`
- `examples/runtime_tools`
- `examples/custom_web`
- `examples/sleep_watchdog`

实机 WiFi、Web、OTA、LittleFS、Watchdog 和 deep sleep 行为仍需连接硬件后确认。
