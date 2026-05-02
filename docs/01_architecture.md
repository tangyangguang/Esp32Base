# 架构说明

## 1. 总体结构

`Esp32Base` 采用一个入口、四个能力模块的结构：

```text
Esp32Base
├── Esp32BaseConfig
├── Esp32BaseNetwork
├── Esp32BaseWeb
└── Esp32BaseRuntime
```

应用项目只需要在 `setup()` 中调用 `Esp32Base::begin()`，在 `loop()` 中持续调用 `Esp32Base::handle()`。

## 2. 初始化顺序

`Esp32Base::begin()` 的初始化顺序是：

```text
日志
Config / NVS
Runtime / LittleFS / Reset 诊断
Network / WiFi / NTP / mDNS
Web / 内置路由 / 应用路由 / OTA
```

WiFi 连接、NTP 同步和 mDNS 启动都不会阻塞 `begin()`。

## 3. 主循环职责

`Esp32Base::handle()` 每轮处理：

```text
Config deferred flush
Network 状态机
WebServer handleClient
Runtime Watchdog feed
一次性启动诊断
```

应用自己的业务逻辑可以放在 `loop()` 中，但必须持续调用 `Esp32Base::handle()`。

## 4. 模块边界

`Esp32BaseConfig` 只处理小型 NVS key/value，不做配置 UI。

`Esp32BaseNetwork` 同时负责 WiFi、NTP 和 mDNS，不拆独立公开类。

`Esp32BaseWeb` 负责内置 Web、认证、状态 API、WiFi 设置 API、OTA 和应用扩展路由。OTA 不单独暴露模块，避免认证边界重复。

`Esp32BaseRuntime` 负责系统资源、Watchdog、deep sleep、restart 和 LittleFS 基础文件能力，不做完整文件管理器。

## 5. 可靠性约束

- WiFi 不阻塞连接。
- NTP 不阻塞同步。
- Web Auth 默认开启。
- OTA 受 Web Auth 保护。
- Restart 和 deep sleep 前 flush config。
- Watchdog 默认不启用，由应用显式启用。
- LittleFS 挂载失败不会阻塞启动，但文件 API 会返回失败。
