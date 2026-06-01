# 架构说明

## 1. 总体结构

`Esp32Base` 采用 5 层架构：

```text
Application
  ↓
Esp32Base
  ↓
Layer 4: Update
  ↓
Layer 3: Web
  ↓
Layer 2: Network
  ↓
Layer 1: Runtime
  ↓
Layer 0: Core
  ↓
Arduino ESP32 Core / ESP-IDF
```

依赖只能从高层指向低层。

不允许：

- 低层依赖高层。
- 同层互相强依赖。
- 循环依赖。

## 2. Layer 0: Core

模块：

- `Esp32BaseLog`
- `Esp32BaseConfig`
- `Esp32BaseSystem`

职责：

- 编译期日志过滤。
- NVS 小配置。
- deferred write。
- `flushAll()`。
- heap / min heap / flash / reset reason / wake reason / uptime。
- 统一 restart。
- 重启日志环。

Core 是唯一永远启用的层。

Core 不包含 Event Bus。Bus 是 Runtime 可选模块。

## 3. Layer 1: Runtime

模块：

- `Esp32BaseBus`
- `Esp32BaseWatchdog`
- `Esp32BaseSleep`
- `Esp32BaseFs`
- `Esp32BaseFileLog`
- `Esp32BaseAppEventLog`
- `Esp32BaseHealth`

职责：

- loop-task-only 同步事件总线。
- Task Watchdog。
- Sleep。
- LittleFS。
- 系统诊断日志 sink（`Esp32BaseFileLog`），依赖 Fs，通过 Core Log 的 line sink 接收日志。
- 应用事件日志，依赖 Fs，提供业务项目可复用的结构化事件环形存储，不解释业务语义。
- 健康诊断。

Runtime 只依赖 Core。

`Esp32BaseLog` 仍属于 Core，只负责 Serial、格式化和 sink 分发，不包含 LittleFS。系统诊断日志由 Runtime 的 `Esp32BaseFileLog` 承担，避免 Core 依赖 FS。

## 4. Layer 2: Network

模块：

- `Esp32BaseWiFi`
- `Esp32BaseDns`
- `Esp32BaseNtp`
- `Esp32BaseMdns`

职责：

- WiFi STA / AP 状态机。
- Captive Portal DNS。
- NTP。
- mDNS。

Network 可使用 Bus，但不强依赖 Bus。

## 5. Layer 3: Web

公开模块：

- `Esp32BaseWeb`

内部模块：

- `src/web/Esp32BaseWeb.cpp`：公开 facade、启动编排、内置路由注册。
- `src/web/internal/WebContext.*`：WebServer、路由表、导航、Auth、上传、请求和响应运行态。
- `src/web/internal/WebResponse.cpp`：HTTP header、chunked 输出、HTML/JSON/CSV escape、断连和 watchdog 喂狗。
- `src/web/internal/WebLayout.cpp`：导航、header/footer、panel、metric、pagination 等 HTML 组件。
- `src/web/internal/WebAssets.cpp`：`/esp32base/ui.css`、基础 head 片段和页面级 PROGMEM 资源。
- `src/web/internal/WebStatus.cpp`、`WebWifi.cpp`、`WebAuth.cpp`、`WebTools.cpp`、`WebLogs.cpp`、`WebAppEvents.cpp`、`WebFs.cpp`、`WebOta.cpp`、`WebAppConfig.cpp`：按功能分组的 handler。

职责：

- Arduino `WebServer`。
- Basic Auth。
- 路由表。
- 状态 API。
- WiFi 配网页面和 API。
- JSON helper。

Web 依赖 WiFi。

Web 是多编译单元实现，不再通过单个巨大 `.inc` 承载全部页面。`library.json` 必须显式编译 `src/web/*.cpp` 和 `src/web/internal/*.cpp`；每个 Web 源文件仍用 profile 宏保护，确保 CORE/RUNTIME/NET 等非 Web profile 不链接 WebServer、Update、LittleFS 等重依赖。

Web 启动条件：

```text
WiFi connected OR WiFi config_portal
```

## 6. Layer 4: Update

公开模块：

- `Esp32BaseOta`

内部 handler group：

- Web OTA route hooks。
- OTA upload progress API hooks。
- OTA auth binding hooks。

职责：

- OTA 状态机。
- SHA256。
- Watchdog 联动。
- Config flush pause。
- rollback。
- Web OTA 路由。

Update 依赖 WiFi + Web。

## 7. 统一入口

`Esp32Base` 负责：

- 固件信息。
- hostname。
- profile 字符串。
- `begin()`。
- `handle()`。
- 延迟启动。
- 启动诊断。
- 资源日志。

`Esp32Base` 不负责：

- WiFi 凭证保存细节。
- Web 路由实现。
- OTA 写入细节。
- 文件 API。
- Watchdog 配置细节。
- Sleep wake source 细节。

## 8. begin 顺序

`Esp32Base::begin()` 固定顺序：

1. Log
2. Config
3. System
4. Bus，如启用
5. Fs，如启用
6. AppEvents，如启用
7. FileLog，如启用
8. Watchdog，如启用
9. Sleep，如启用
10. Health，如启用
11. WiFi，如启用
12. 标记 ready

`begin()` 不等待网络相关状态。

## 9. begin 失败策略

Core 失败：

- Log / Config / System 失败视为不可恢复。
- 记录错误。
- `begin()` 返回 false，由应用决定是否 halt 或进入降级流程。

可选模块失败：

- 默认不 halt。
- 记录错误。
- 标记模块 failed。
- 继续启动其他不依赖它的模块。

严格模式：

```cpp
ESP32BASE_STRICT_OPTIONAL_BEGIN=0
```

设为 `1` 时，可选模块失败会使 `begin()` 失败。

## 10. handle 顺序

`Esp32Base::handle()` 固定顺序：

1. 如果 OTA 正在上传，跳过 Config deferred flush。
2. 否则 Config deferred flush，一轮最多一条。
3. FileLog handle，如启用。
4. WiFi handle。
5. Captive DNS handle。
6. 条件启动 Web。
7. 条件启动 NTP。
8. 条件启动 mDNS。
9. 条件启动 OTA 网络服务（ArduinoOTA/espota）。
10. Web handle。
11. OTA handle，包含 mark-valid timeout 检查。
12. Health handle。
13. Watchdog feed。
14. 一次性启动诊断日志。

LittleFS 当前没有 maintenance 任务，不在 handle 中做额外维护。

OTA boot 诊断和 rollback/mark-valid 状态不属于条件网络服务；它在 `Esp32Base::begin()` 的 system 模块之后立即初始化，避免 WiFi/Web 未就绪时跳过 rollback timeout。

## 11. 生命周期

所有重启必须走：

```cpp
Esp32BaseSystem::restart(const char* reason);
```

所有 deep sleep 必须走：

```cpp
Esp32BaseSleep::deepSleep(...);
```

流程：

1. 发布生命周期事件，如 Bus 启用。
2. 写重启日志环。
3. `Esp32BaseConfig::flushAll()`。
4. 输出最后诊断。
5. 处理 Watchdog。
6. 执行底层 restart / sleep。

禁止模块内部直接调用：

- `ESP.restart()`
- `esp_restart()`
- `esp_deep_sleep_start()`

例外：`Esp32BaseSystem` 是 restart 的唯一底层执行点，`Esp32BaseSleep` 是 deep sleep 的唯一底层执行点；其他模块必须通过这两个生命周期入口间接触发。

## 12. 目录结构

最终源码结构：

```text
src/
├── Esp32Base.h
├── Esp32Base.cpp
├── Esp32BaseProfile.h
├── core/
├── runtime/
├── network/
├── web/
└── update/
```

不提供全局短别名头文件，避免 `Log`、`Config`、`Web` 等名字污染应用全局命名空间。

## 13. Profile Unity Implementation

Core 使用标准 `.cpp` 编译单元，始终参与编译。

可选模块使用 `.inc` 实现文件，并且只由 `Esp32Base.cpp` 在 profile 条件成立时包含：

```cpp
#if ESP32BASE_ENABLE_WIFI
#include "network/Esp32BaseWiFi.inc"
#endif
```

这个模型的目标是：

- 关闭模块不进入编译单元。
- 避免构建系统把可选模块实现当作独立 `.cpp` 编译。
- 让 profile 裁剪结果可以通过构建日志和 map 文件直接验证。

`.inc` 文件不得被应用直接 include，也不得声明公开 API；公开 API 只放在对应 `.h` 文件中。
