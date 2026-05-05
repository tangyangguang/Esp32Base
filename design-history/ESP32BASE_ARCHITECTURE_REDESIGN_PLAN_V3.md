# Esp32Base 架构重构方案 V3

## 1. 方案定位

本方案是 `Esp32Base` 的最终重构蓝图，已吸收：

- `ESP32BASE_ARCHITECTURE_REDESIGN_PLAN.md`
- `ESP32BASE_ARCHITECTURE_REDESIGN_REVIEW.md`
- `ESP32BASE_ARCHITECTURE_REDESIGN_PLAN_V2.md`
- `ESP32BASE_ARCHITECTURE_REDESIGN_REVIEW_V2.md`

本方案的目标不是做一个以后持续大改的框架，而是 **一次构建一个成熟、清晰、资源可控、适合长期维护的 ESP32 基础库**。完成后原则上只做 bug 修复、兼容性维护、文档修正和必要的小范围实机适配，不再做大规模架构迭代。

允许：

- 删除并重写现有源码。
- 破坏当前 API。
- 重命名模块。
- 重写示例、文档、测试和分区建议。

不追求：

- 兼容旧 `EspBase` API。
- 兼容当前 `Esp32Base` API。
- 支持 ESP8266。
- 做成 IoT 云框架、Web 平台或大型组件系统。

设计目标：

- 极简应用只付 Core 成本。
- 常见量产设备有明确 profile。
- 未启用模块不编译、不链接、不初始化。
- 模块依赖方向固定且单向。
- 所有启动路径非阻塞。
- OTA、NVS、Watchdog、LittleFS、WiFi 配网具备量产可靠性。
- ESP32 / ESP32-S3 / ESP32-C3 都是一等目标。
- Arduino Core 2.x / 3.x 差异在设计中显式处理。

## 2. 最终模块顺序

模块按 5 层组织。依赖只能从高层指向低层，不允许反向依赖或循环依赖。

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

### 2.1 Layer 0: Core

Core 永远启用，是最小基础能力。

模块：

- `Esp32BaseLog`
- `Esp32BaseConfig`
- `Esp32BaseSystem`

职责：

- 编译期日志级别过滤。
- NVS 小配置。
- deferred write。
- `flushAll()`。
- heap / min heap / flash / reset reason / wake reason / uptime。
- 统一 restart。
- OTA app valid / rollback 底层封装。
- 重启日志环。

Core 不包含：

- Bus。
- WiFi。
- DNS。
- NTP。
- mDNS。
- Web。
- OTA。
- LittleFS。
- Watchdog。
- Sleep。
- Health。

### 2.2 Layer 1: Runtime

Runtime 是可选设备运行能力层。

模块：

- `Esp32BaseBus`
- `Esp32BaseWatchdog`
- `Esp32BaseSleep`
- `Esp32BaseFs`
- `Esp32BaseHealth`

职责：

- 同步事件总线。
- Task Watchdog。
- deep sleep / light sleep。
- LittleFS。
- 周期健康诊断。

约束：

- 只依赖 Core。
- 不依赖 Network / Web / Update。
- 关闭 Runtime 能力时不得产生对应静态表、全局对象或链接依赖。

### 2.3 Layer 2: Network

Network 是联网能力层。

模块：

- `Esp32BaseWiFi`
- `Esp32BaseDns`
- `Esp32BaseNtp`
- `Esp32BaseMdns`

职责：

- WiFi STA 状态机。
- AP config portal。
- Captive Portal DNS。
- 重连 backoff。
- NTP。
- mDNS。

约束：

- 依赖 Core。
- 可选使用 Bus，但不强依赖 Bus。
- NTP / mDNS 仅在 STA connected 后启动。
- DNS 仅在 config portal 模式启动。

### 2.4 Layer 3: Web

Web 是局域网管理层。

公开模块：

- `Esp32BaseWeb`

内部模块：

- `Esp32BaseWebStatus`
- `Esp32BaseWebWiFi`
- `Esp32BaseWebUtil`

职责：

- Arduino `WebServer`。
- Basic Auth。
- 自定义路由。
- 状态 API。
- WiFi 配网页面/API。
- JSON 输出和转义。

约束：

- 依赖 WiFi。
- 不强依赖 Bus。
- Web 启动条件必须是：

```text
WiFi connected OR WiFi config_portal
```

配置门户 AP 模式下 Web 必须能启动，否则用户无法访问配网页面。

### 2.5 Layer 4: Update

Update 是固件升级层。

公开模块：

- `Esp32BaseOta`

内部模块：

- `Esp32BaseWebOta`

职责：

- OTA 状态机。
- 固件写入。
- SHA256 校验。
- Watchdog 联动。
- OTA 期间暂停 NVS deferred flush。
- Web OTA 页面/API。
- 双 OTA 分区 mark valid / rollback。

约束：

- 依赖 WiFi + Web。
- 不允许默认认证启动 OTA。
- 未授权请求不能进入 flash 写入流程。
- OTA 成功后走统一 restart。

## 3. 最终 Profile 集合

Profile 是预验证的典型场景组合。底层 `ESP32BASE_ENABLE_*` 仍可精细覆盖。

默认 profile：`ESP32BASE_PROFILE_CORE`。

### 3.1 ESP32BASE_PROFILE_CORE

启用：

- Log
- Config
- System

不启用：

- Bus
- Runtime
- WiFi
- Web
- OTA
- LittleFS
- Watchdog
- Sleep

目标：

- 不链接 WiFi。
- 不链接 WebServer。
- 不链接 Update。
- 不链接 LittleFS。
- 不启动 AP。
- 不挂载文件系统。

### 3.2 ESP32BASE_PROFILE_RUNTIME

启用：

- CORE
- Bus
- Watchdog
- Sleep
- Fs
- Health

适合：

- 离线设备。
- 本地文件。
- 低功耗。
- 看门狗。
- 运行健康诊断。

### 3.3 ESP32BASE_PROFILE_NET

启用：

- CORE
- WiFi
- DNS
- NTP
- mDNS

默认不启用：

- Bus
- Watchdog
- Sleep
- Fs
- Health
- Web
- OTA

说明：

- NET profile 默认不启用 Bus。
- 如果业务需要事件订阅，可显式设置 `ESP32BASE_ENABLE_BUS=1`。

适合：

- 纯联网设备。
- 业务自己使用 MQTT、HTTP Client 或其他通信库。

### 3.4 ESP32BASE_PROFILE_NET_RUNTIME

启用：

- NET
- Bus
- Watchdog
- Sleep
- Fs
- Health

适合：

- WiFi + NTP + Watchdog + LittleFS，但不需要 Web / OTA 的量产设备。
- 传感器、执行器、采集节点。
- 业务层自带 MQTT / HTTP Client 的设备。

这是推荐的生产联网节点 profile。

### 3.5 ESP32BASE_PROFILE_WEB

启用：

- NET
- Bus
- Web
- WebStatus
- WebWiFi

不启用：

- OTA
- Fs
- Watchdog
- Sleep
- Health

说明：

- WEB profile 默认启用 Bus，便于 Web 诊断和应用事件集成。
- Web 模块本身不强依赖 Bus。
- 用户可显式设置 `ESP32BASE_ENABLE_BUS=0` 关闭 Bus。

适合：

- 需要局域网配置和状态查看的设备。
- 不需要 OTA 和本地文件能力的 Web 管理型应用。

### 3.6 ESP32BASE_PROFILE_WEB_RUNTIME

启用：

- WEB
- Watchdog
- Sleep
- Fs
- Health

不启用：

- OTA

适合：

- Web 配置 + Watchdog + Fs，但不需要 OTA 的量产设备。
- 固件升级走烧录器或业务私有通道的设备。

这是推荐的无 OTA Web 设备 profile。

### 3.7 ESP32BASE_PROFILE_FULL

启用：

- WEB_RUNTIME
- OTA
- WebOta

适合：

- 完整 Web 管理。
- Web OTA。
- 本地文件。
- Watchdog。
- 低功耗。
- 运行健康诊断。

## 4. Profile 与底层宏契约

底层宏：

- `ESP32BASE_ENABLE_LOG`
- `ESP32BASE_ENABLE_CONFIG`
- `ESP32BASE_ENABLE_SYSTEM`
- `ESP32BASE_ENABLE_BUS`
- `ESP32BASE_ENABLE_WATCHDOG`
- `ESP32BASE_ENABLE_SLEEP`
- `ESP32BASE_ENABLE_FS`
- `ESP32BASE_ENABLE_HEALTH`
- `ESP32BASE_ENABLE_WIFI`
- `ESP32BASE_ENABLE_DNS`
- `ESP32BASE_ENABLE_NTP`
- `ESP32BASE_ENABLE_MDNS`
- `ESP32BASE_ENABLE_WEB`
- `ESP32BASE_ENABLE_WEB_STATUS`
- `ESP32BASE_ENABLE_WEB_WIFI`
- `ESP32BASE_ENABLE_OTA`
- `ESP32BASE_ENABLE_WEB_OTA`

### 4.1 展开顺序

规则：

1. 用户选择 profile。
2. `Esp32BaseProfile.h` 展开 profile 默认值。
3. 用户显式定义的 `ESP32BASE_ENABLE_*` 保留优先级。
4. 所有展开完成后执行依赖检查。

实现要求：

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

禁止 profile 展开直接覆盖用户显式 `-D`。

### 4.2 依赖检查

编译期依赖：

- `WEB` 需要 `WIFI`。
- `WEB_STATUS` 需要 `WEB`。
- `WEB_WIFI` 需要 `WEB` + `WIFI`。
- `OTA` 需要 `WIFI`。
- `WEB_OTA` 需要 `WEB` + `OTA`。
- `DNS` 需要 `WIFI`。
- `NTP` 需要 `WIFI`。
- `MDNS` 需要 `WIFI`。

软依赖：

- `HEALTH` 可不依赖 Bus；未启用 Bus 时只提供查询 API，不发布事件。
- `WEB` 可不依赖 Bus；未启用 Bus 时不发布 Web 事件。
- `WIFI` 可不依赖 Bus；未启用 Bus 时不发布 WiFi 事件。

### 4.3 关闭模块的硬要求

关闭模块必须满足：

- `.cpp` 整体不编译对应实现。
- 不产生全局对象。
- 不 include 重依赖头。
- 不链接对应 Arduino 库。
- `Esp32Base::begin()` / `handle()` 不调用该模块。

资源验证必须通过 map 文件确认。

## 5. 公开 API 形态

### 5.1 头文件

提供：

- `Esp32Base.h`
- `Esp32BaseAll.h`
- `Esp32BaseAliases.h`

`Esp32Base.h`：

- 默认推荐入口。
- 包含 profile 配置。
- 始终包含 Core。
- 按启用宏包含可用模块。

`Esp32BaseAll.h`：

- 用于示例和诊断。
- 聚合所有模块声明。
- 不改变模块编译裁剪。

`Esp32BaseAliases.h`：

- 提供短命名别名。
- 只做类型别名，无运行时成本。

示例：

```cpp
namespace esp32base {
using Log = Esp32BaseLog;
using Config = Esp32BaseConfig;
using System = Esp32BaseSystem;
using WiFi = Esp32BaseWiFi;
using Web = Esp32BaseWeb;
using Ota = Esp32BaseOta;
}
```

### 5.2 公开类

Core:

- `Esp32Base`
- `Esp32BaseLog`
- `Esp32BaseConfig`
- `Esp32BaseSystem`

Runtime:

- `Esp32BaseBus`
- `Esp32BaseWatchdog`
- `Esp32BaseSleep`
- `Esp32BaseFs`
- `Esp32BaseHealth`

Network:

- `Esp32BaseWiFi`
- `Esp32BaseDns`
- `Esp32BaseNtp`
- `Esp32BaseMdns`

Web:

- `Esp32BaseWeb`

Update:

- `Esp32BaseOta`

不公开为主 API：

- `Esp32BaseWebStatus`
- `Esp32BaseWebWiFi`
- `Esp32BaseWebOta`
- `Esp32BaseWebUtil`

这些内部模块只负责注册和实现内置路由。

## 6. 统一入口与启动策略

`Esp32Base` 负责：

- 固件信息。
- hostname。
- profile 字符串。
- begin / handle。
- 延迟启动。
- 启动诊断。
- 资源日志。

`Esp32Base` 不负责：

- WiFi 凭证明细。
- Web 路由实现。
- OTA 写入细节。
- 文件 API。
- Watchdog 配置细节。
- Sleep wake source 细节。

### 6.1 begin 顺序

`Esp32Base::begin()` 顺序：

1. Log。
2. Config。
3. System。
4. Bus，如启用。
5. Fs，如启用。
6. Watchdog，如启用。
7. Sleep，如启用。
8. Health，如启用。
9. WiFi，如启用。
10. 标记 ready。

不等待：

- WiFi 连接。
- NTP 同步。
- mDNS 启动。
- Web 启动。
- OTA ready。

### 6.2 begin 失败策略

Core 失败：

- Log / Config / System 初始化失败视为不可恢复。
- 记录错误。
- 进入 halt。

可选模块失败：

- Fs / Watchdog / Sleep / Health / WiFi / Web / OTA 初始化失败，不应默认 halt。
- 记录错误。
- 标记模块 failed。
- 继续启动其他不依赖该模块的能力。

例外：

- 用户可通过宏开启严格模式：

```cpp
ESP32BASE_STRICT_OPTIONAL_BEGIN=0
```

当设为 `1` 时，启用模块初始化失败会导致 `Esp32Base::begin()` 返回 false 或 halt，具体策略在文档中明确。

默认：

- Core 失败 halt。
- 可选模块失败继续。

### 6.3 handle 顺序

`Esp32Base::handle()` 顺序：

1. 如果 OTA 正在上传，跳过 Config deferred flush。
2. 否则 Config deferred flush，一轮最多一条。
3. WiFi handle，如启用。
4. Captive DNS handle，如启用且处于 config portal。
5. 条件启动 Web：WiFi connected 或 WiFi config portal。
6. 条件启动 NTP：WiFi connected。
7. 条件启动 mDNS：WiFi connected。
8. 条件启动 OTA：WiFi connected + Web ready + OTA enabled。
9. Web handle，如 ready。
10. OTA handle，如 ready。
11. Health handle，如启用。
12. Watchdog feed，如启用。
13. Fs maintenance，如需要。
14. 一次性启动诊断日志。

设计取舍：

- 第一版最终实现采用硬编码顺序 + `#if ESP32BASE_ENABLE_*`。
- 不引入静态注册表。
- 这是最终 v1 架构选择，不作为后续大规模演进项。

## 7. 生命周期与重启

所有重启必须走：

```cpp
Esp32BaseSystem::restart(const char* reason);
```

所有 deep sleep 必须走：

```cpp
Esp32BaseSleep::deepSleep(...);
```

统一流程：

1. 发布 `system.restart` 或 `system.sleep` 事件，如 Bus 启用。
2. 写入重启日志环。
3. `Esp32BaseConfig::flushAll()`。
4. 输出最后诊断日志。
5. 处理 Watchdog。
6. 执行 `esp_restart()` 或 `esp_deep_sleep_start()`。

禁止模块内部直接调用：

- `ESP.restart()`
- `esp_restart()`
- `esp_deep_sleep_start()`

CI 增加文本检查。

### 7.1 重启日志环

Core 提供轻量重启日志环，存储在 NVS。

记录字段：

- reason。
- uptime。
- reset reason。
- free heap。
- timestamp，如 NTP 已同步。

默认条数：

```cpp
ESP32BASE_RESTART_LOG_CAPACITY=4
```

目标：

- 量产排障。
- OTA 失败定位。
- Watchdog reset 分析。

## 8. Event Bus

`Esp32BaseBus` 是 Runtime 可选模块，不属于 Core。

默认容量：

```cpp
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define ESP32BASE_BUS_MAX_SUBSCRIBERS 12
#else
#define ESP32BASE_BUS_MAX_SUBSCRIBERS 16
#endif
```

语义：

- 同步发布。
- 固定容量订阅表。
- 只允许在 Arduino loop 任务 publish。
- 不面向 ISR。
- 不作为异步任务队列。

多任务规则：

- WiFi / system event callback 只记录轻量状态或入固定队列。
- 实际 publish 在模块 `handle()` 中执行。
- 第一版不支持跨任务直接 publish。

事件名：

- 模块必须导出 `static constexpr const char*` 事件常量。
- 文档中禁止用户手写裸字符串作为推荐用法。

核心事件：

- `wifi.connected`
- `wifi.disconnected`
- `wifi.config_portal`
- `web.ready`
- `ota.start`
- `ota.progress`
- `ota.success`
- `ota.failed`
- `system.restart`
- `watchdog.reset_detected`
- `health.tick`

## 9. Core 详细设计

### 9.1 Log

要求：

- 编译期日志级别过滤。
- 被关闭级别的 format 字符串不进入 `.rodata`。
- 默认串口波特率可配置。
- 支持 bytes、duration、uptime 格式化。
- 支持启动延迟可配置。

### 9.2 Config

后端：

- ESP32 NVS。
- 不保留跨芯片 HAL。

命名限制：

- namespace 长度 `1..15`。
- key 长度 `1..15`。
- 非法名称返回 false。
- Debug/Warn 日志说明原因。

编译期辅助：

```cpp
ESP32BASE_CONFIG_STATIC_NAME("wifi_ssid")
```

该宏用于常量字符串长度检查。

读取：

- 使用 readonly NVS。
- 读取不存在 namespace 不创建 namespace。

写入：

- 写入前比较旧值。
- 值未变化不写 flash。
- 写入后清对应 pending。

deferred：

- 支持 delay。
- 支持 int / bool / string。
- `handle()` 每轮最多写一条到期 pending。
- `flushAll()` 忽略 delay，写完所有 pending。
- `clearNamespace()` 同时清 pending。

OTA 期间：

- 当 `Esp32BaseOta::isUploading()` 为 true，`Esp32BaseConfig::handle()` 不执行 deferred flush。
- `flushAll()` 仍可在 OTA 完成后的统一 restart 前执行。

建议 API：

```cpp
bool setStr(const char* ns, const char* key, const char* value);
bool getStr(const char* ns, const char* key, char* out, size_t len, const char* def = "");
bool setInt(const char* ns, const char* key, int32_t value);
bool setIntDeferred(const char* ns, const char* key, int32_t value, uint32_t delayMs = 1000);
bool setBool(const char* ns, const char* key, bool value);
bool setBoolDeferred(const char* ns, const char* key, bool value, uint32_t delayMs = 1000);
bool flushOne(bool ignoreDue = false);
bool flushAll();
```

### 9.3 System

职责：

- heap。
- min free heap。
- flash size。
- reset reason。
- wake reason。
- uptime。
- unified restart。
- OTA valid / rollback 底层封装。
- restart log ring。

API：

```cpp
uint32_t freeHeap();
uint32_t minFreeHeap();
uint32_t totalHeap();
uint32_t flashSize();
const char* resetReason();
const char* wakeReason();
void restart(const char* reason);
bool markCurrentAppValid();
bool isRollbackPossible();
void rollbackAndRestart(const char* reason);
```

## 10. Runtime 详细设计

### 10.1 Watchdog

默认：

- 不启用。
- 由 profile 或用户显式开启。

要求：

- 兼容 Arduino Core 2.x 和 3.x 的 TWDT API 差异。
- 支持当前任务 add/delete/reset。
- 支持 OTA 长任务 remove/restore。
- 所有失败路径必须恢复 Watchdog 状态。

API：

```cpp
bool begin(uint32_t timeoutMs);
void feed();
bool removeCurrentTaskForLongOperation();
bool restoreCurrentTaskAfterLongOperation();
bool isEnabled();
bool wasWatchdogReset();
uint32_t lifetimeResetCount();
```

`lifetimeResetCount()` 使用 NVS 记录跨 boot 计数。

### 10.2 Sleep

要求：

- deep sleep 支持 timer wake。
- light sleep 可选支持。
- GPIO wake 根据芯片能力返回 bool。
- ESP32-C3 不提供不可实现的统一承诺。

不支持的 wake source：

- 返回 false。
- 输出 warn。
- 不静默成功。

### 10.3 Fs

默认文件系统：

- LittleFS。

挂载策略：

1. `LittleFS.begin(false)` 尝试挂载。
2. 挂载失败且 `ESP32BASE_FS_AUTO_FORMAT=1` 时格式化。
3. 挂载失败且未开启 auto-format 时记录错误，模块 failed，但系统继续运行。
4. 首次成功挂载后，在 NVS 记录 `fs.formatted=1` 诊断标志。
5. 格式化必须有明确日志。

默认：

```cpp
ESP32BASE_FS_AUTO_FORMAT=0
```

### 10.4 Health

默认采样周期：

```cpp
ESP32BASE_HEALTH_TICK_INTERVAL_MS=30000
```

采样：

- uptime。
- free heap。
- min free heap。
- loop 周期。
- reset reason。
- WiFi 状态，如启用。
- Fs 状态，如启用。

如果 Bus 启用：

- 发布 `health.tick`。

如果 Bus 未启用：

- 只提供查询 API。

## 11. Network 详细设计

### 11.1 WiFi

状态：

- `IDLE`
- `CONNECTING`
- `CONNECTED`
- `CONFIG_PORTAL`
- `RETRY_BACKOFF`
- `FAILED`

规则：

- `begin()` 不阻塞。
- 无凭证进入 config portal。
- 有凭证启动 STA connect。
- 连接超时进入 backoff。
- 支持最大重试次数，默认无限重试。
- 支持 power save。

backoff 默认：

- 前 5 次：15s。
- 6 到 10 次：60s。
- 10 次后：300s。

WiFi event callback：

- 只更新轻量状态或入队。
- 不写 NVS。
- 不 publish Bus。
- 不启动 Web。
- 不做重操作。

### 11.2 Captive DNS

模块：

- `Esp32BaseDns`

职责：

- AP config portal 模式启动 DNSServer。
- 拦截常见域名解析到 AP IP。
- config portal 退出时停止 DNS。

必须验证系统：

- iOS。
- Android。
- macOS。
- Windows。

拦截目标：

- `captive.apple.com`
- `connectivitycheck.gstatic.com`
- `clients3.google.com`
- 通配其他域名到 AP IP。

路由策略：

- 对 captive 检测 URL 返回能触发门户弹出的 200/302 响应。
- 非内置路径可重定向到 `/esp32base/wifi`。

### 11.3 NTP

规则：

- WiFi connected 后启动。
- 不阻塞等待同步。
- 支持 1 到 3 个 server。
- 默认 UTC。
- 时区由应用配置。
- 未同步时 `formatTime()` 返回 false。

### 11.4 mDNS

规则：

- WiFi connected 后启动。
- hostname 来自 `Esp32Base::setHostname()`。
- Web 启用时注册 HTTP service。
- WiFi 断开时停止或标记不运行。
- 重连后恢复。
- Arduino Core 3.x 下 stop/start 必须实测。

## 12. Web 详细设计

### 12.1 WebServer

采用 Arduino `WebServer`。

不采用 AsyncWebServer。

理由：

- 依赖少。
- 行为可控。
- 符合小型管理页定位。
- 更适合一次定型后长期维护。

### 12.2 路由

要求：

- 固定容量路由表。
- 应用路由可在 `Esp32Base::begin()` 前或后注册。
- 内置路由和应用路由共享认证边界。
- 自定义 handler 建议 < 200ms，长任务应异步拆分或返回状态。

默认容量：

```cpp
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define ESP32BASE_WEB_MAX_ROUTES 12
#else
#define ESP32BASE_WEB_MAX_ROUTES 16
#endif
```

### 12.3 认证

默认：

- Web Auth 开启。
- 默认开发账号密码可存在。

OTA 安全规则：

- `Esp32BaseWeb::setAuth()` 必须由应用显式调用。
- `Esp32BaseWeb` 内部维护 `_authSetByApplication`。
- OTA 路由启动前检查：
  - Auth enabled。
  - `_authSetByApplication == true`。
- 不满足则 OTA route 不注册，并输出 error。

不使用“字符串是否等于默认密码”作为唯一判断。

原因：

- 默认密码可能被宏覆盖。
- 用户修改用户名但不修改密码无法可靠判断。
- flag 语义更清晰。

文档必须说明：

- HTTP Basic Auth 明文传输。
- 它用于防误操作，不用于抵御主动攻击。
- 生产固件必须显式设置认证信息。

### 12.4 JSON 输出

必须提供：

```cpp
void beginJson(int code);
void writeJsonEscaped(const char* text);
void endJson();
void sendJson(int code, const char* json);
```

必须 escape：

- `"`
- `\`
- `\n`
- `\r`
- `\t`
- 控制字符 `< 0x20`

内置 JSON 不得直接拼接未 escape 的：

- hostname。
- firmware name。
- firmware version。
- WiFi SSID。
- IP 文本。
- error 文本。
- 文件内容。

### 12.5 内置路由

Web 路由：

- `GET /esp32base`
- `GET /esp32base/api/status`
- `GET /esp32base/api/chip`
- `GET /esp32base/api/firmware`
- `GET /esp32base/wifi`
- `POST /esp32base/wifi`
- `POST /esp32base/api/wifi`
- `POST /esp32base/api/wifi/clear`

OTA 路由：

- `GET /esp32base/ota`
- `POST /esp32base/ota`
- `GET /esp32base/api/ota`

OTA 路由只在 OTA 启用且认证条件满足时注册。

## 13. OTA 详细设计

### 13.1 状态机

状态：

- `IDLE`
- `READY`
- `UPLOADING`
- `VERIFYING`
- `SUCCESS`
- `FAILED`

记录：

- progress。
- bytesProcessed。
- totalSize。
- expectedSha256。
- calculatedSha256。
- lastError。
- startTime。

### 13.2 上传协议

规则：

- 上传开始前必须认证成功。
- 未授权请求不得调用 `Update.begin()`。
- 进入 `UPLOADING` 后暂停 Config deferred flush。
- 每个 chunk 后调用 `yield()`。
- 写入失败立即进入 `FAILED`。
- 完整写入后进入 `VERIFYING`。
- SHA256 校验通过后才能进入 `SUCCESS`。
- 成功后走统一 restart。
- 失败、abort、success 都必须恢复 Watchdog 和 Config flush 状态。

### 13.3 SHA256 校验

支持可选 SHA256。

API：

```cpp
bool startUpload(size_t totalSize, const char* expectedSha256Hex = nullptr);
```

Web OTA 可从以下来源读取摘要：

- HTTP header: `X-Sha256`
- form field: `sha256`

规则：

- 如果提供 `expectedSha256Hex`，必须校验。
- 摘要格式必须是 64 字符 hex。
- 校验失败不得标记成功。
- 校验失败不得重启到新固件。

第一版最终实现必须包含 SHA256 可选校验。

### 13.4 Watchdog 联动

如果 Watchdog 启用：

1. OTA 写入前调用 `Esp32BaseWatchdog::removeCurrentTaskForLongOperation()`。
2. 每个 chunk 后 `yield()`。
3. OTA 完成、失败、abort 后调用 `restoreCurrentTaskAfterLongOperation()`。
4. 所有失败路径都必须恢复 Watchdog。

### 13.5 Brownout 策略

默认不关闭 brownout detector。

可选宏：

```cpp
ESP32BASE_OTA_DISABLE_BROWNOUT_DURING_WRITE=0
```

仅用户显式开启时，OTA 写入期间临时关闭 brownout，结束后恢复。

文档必须说明风险：

- 关闭 brownout 可能掩盖供电问题。
- 不建议生产默认开启。

### 13.6 OTA 回滚

支持 ESP32 双 OTA 分区回滚。

API：

```cpp
bool Esp32BaseOta::markCurrentValid();
bool Esp32BaseOta::isRollbackPossible();
void Esp32BaseOta::rollbackAndRestart(const char* reason);
```

策略：

- 新固件启动后，应用应在自检通过后调用 `markCurrentValid()`。
- FULL profile 可启用自动 mark valid timeout。

宏：

```cpp
ESP32BASE_OTA_REQUIRE_MARK_VALID=0
ESP32BASE_OTA_MARK_VALID_TIMEOUT_MS=30000
```

当 `REQUIRE_MARK_VALID=1` 且超时未标记：

- 写重启日志。
- rollback。
- restart。

## 14. Arduino Core 与芯片差异

### 14.1 支持范围

支持：

- Arduino ESP32 Core 2.0.14 及以上。
- Arduino ESP32 Core 3.0.4 及以上。

必须使用：

- `ESP_ARDUINO_VERSION_MAJOR`
- `ESP_ARDUINO_VERSION_MINOR`
- `CONFIG_IDF_TARGET_ESP32`
- `CONFIG_IDF_TARGET_ESP32S3`
- `CONFIG_IDF_TARGET_ESP32C3`

### 14.2 已知差异

Watchdog：

- Core 2.x：`esp_task_wdt_init(timeout, panic)`。
- Core 3.x：`esp_task_wdt_init(const esp_task_wdt_config_t*)`。

WiFi event：

- Core 2.x：`SYSTEM_EVENT_*`。
- Core 3.x：`ARDUINO_EVENT_*`。

Sleep：

- ESP32 / ESP32-S3：RTC GPIO 能力较完整。
- ESP32-C3：wake source 受限。

PSRAM：

- 第一版不依赖 PSRAM。
- 不为 PSRAM 优化默认路径。

容量默认：

- ESP32-C3 的 route、Bus、pending、Web buffer 默认低于 ESP32/S3。
- 所有容量可宏覆盖。

## 15. 目录结构

最终目录：

```text
src/
├── Esp32Base.h
├── Esp32BaseAll.h
├── Esp32BaseAliases.h
├── Esp32Base.cpp
├── Esp32BaseProfile.h
├── core/
│   ├── Esp32BaseLog.*
│   ├── Esp32BaseConfig.*
│   └── Esp32BaseSystem.*
├── runtime/
│   ├── Esp32BaseBus.*
│   ├── Esp32BaseWatchdog.*
│   ├── Esp32BaseSleep.*
│   ├── Esp32BaseFs.*
│   └── Esp32BaseHealth.*
├── network/
│   ├── Esp32BaseWiFi.*
│   ├── Esp32BaseDns.*
│   ├── Esp32BaseNtp.*
│   └── Esp32BaseMdns.*
├── web/
│   ├── Esp32BaseWeb.*
│   ├── Esp32BaseWebStatus.*
│   ├── Esp32BaseWebWiFi.*
│   └── Esp32BaseWebUtil.*
└── update/
    ├── Esp32BaseOta.*
    └── Esp32BaseWebOta.*
```

## 16. 示例项目

示例按 profile 组织：

- `examples/core_basic`
- `examples/runtime_basic`
- `examples/wifi_basic`
- `examples/net_runtime`
- `examples/web_config`
- `examples/web_runtime`
- `examples/web_ota`
- `examples/full_diagnostics`

每个示例包含：

- ESP32。
- ESP32-S3。
- ESP32-C3。

示例约束：

- CORE 示例不得 include WiFi/Web/Fs/Ota。
- NET 示例不得 include Web/Ota。
- WEB 示例不得 include Ota。
- FULL 示例必须覆盖 Ota、Watchdog、Fs、Health。

## 17. library.json 策略

PlatformIO `library.json` 不能真正按 profile 动态切依赖，因此采取以下策略：

- 不依赖 `library.json` 强行拉入重模块。
- 源码只在对应 `ESP32BASE_ENABLE_*` 为 1 时 include 重依赖头。
- 通过 map 文件验证关闭 profile 不链接重符号。
- 文档明确：资源裁剪以编译产物为准，而不是 `library.json` 元数据。

如果 PlatformIO 元数据导致 CORE 用户被无意义依赖污染，应移除非必要依赖声明，仅保留 `frameworks` / `platforms` / `includeDir`。

## 18. 测试与验收

### 18.1 Phase 0 原型验证

实施正式重写前必须完成。

原型 1：Profile 裁剪。

- 7 个空 main。
- 分别启用 7 个 profile。
- 检查 map 文件。
- CORE 不得链接 WiFi/WebServer/Update/LittleFS。
- NET 不得链接 WebServer/Update。
- WEB 不得链接 Update。

原型 2：Arduino Core 2.x / 3.x。

- 2.0.14。
- 3.0.4。
- ESP32 / ESP32-S3 / ESP32-C3。
- 验证 Watchdog、WiFi event、Sleep 条件编译。

原型 3：OTA + Watchdog + NVS pause + SHA256。

- 大固件上传。
- remove/restore task。
- OTA 期间高频 NVS 写入。
- SHA256 正确和错误两种路径。

原型 4：WiFi event 入队。

- WiFi callback 只入队。
- loop 中 publish。
- 测事件延迟和丢失。

原型 5：Captive Portal。

- iPhone。
- Android。
- macOS。
- Windows。
- 验证 AP 自动弹窗或自动跳转。

### 18.2 编译矩阵

矩阵：

- 7 个 profile。
- ESP32。
- ESP32-S3。
- ESP32-C3。
- Arduino Core 2.x。
- Arduino Core 3.x。

CI 从 Phase 1 开始建立，每个后续阶段扩展，不留到最后。

### 18.3 实机验收

必须通过：

- CORE：无 AP、无 Web、无 FS 挂载。
- RUNTIME：Watchdog、Sleep、LittleFS 文件读写。
- NET：AP 配网到 STA。
- NET_RUNTIME：WiFi + Watchdog + Fs 长时间运行。
- WEB：Auth、状态 API、配网 API、自定义路由。
- WEB_RUNTIME：Web + Fs + Watchdog，无 OTA。
- FULL：OTA 成功、未授权拒绝、Watchdog 安全。
- OTA 默认认证拒绝：不调用 `setAuth()` 时 OTA route 不注册。
- OTA SHA256：篡改 1 字节必须失败。
- OTA 中途断电：30% / 70% / 99%。
- OTA 后未 markValid：可回滚。
- OTA 期间 1Hz NVS 写入：deferred flush pause 生效。
- Captive Portal 多系统弹出。
- 路由器掉电 5 分钟后恢复。
- NVS 写满。
- LittleFS 首次挂载失败不 halt。
- JSON escape。
- ESP32-C3 单核重负载。
- mDNS 多次重连恢复。
- deferred write + restart 全部落盘。
- brownout during OTA 不变砖。

### 18.4 资源验收

每个 profile 输出：

- firmware size。
- text / data / bss。
- static RAM。
- 启动后 free heap。
- min free heap。
- WiFi connected free heap。
- Web request min heap。
- OTA min heap。
- LittleFS 操作 min heap。

资源表写入文档。

## 19. 实施阶段

### Phase 0: 原型和风险冻结

完成 5 个原型。

输出：

- 资源裁剪报告。
- Arduino Core 兼容报告。
- OTA 可靠性报告。
- Captive Portal 报告。

Phase 0 不通过，不进入正式重写。

### Phase 1: Core

实现：

- `Esp32BaseProfile.h`
- `Esp32BaseLog`
- `Esp32BaseConfig`
- `Esp32BaseSystem`
- `Esp32Base`
- `Esp32BaseAliases.h`
- `examples/core_basic`
- CI 基础矩阵。

验收：

- CORE 三芯片编译。
- CORE 不链接重模块。
- Profile 宏覆盖反例测试。

### Phase 2: Runtime

实现：

- Bus。
- Watchdog。
- Sleep。
- Fs。
- Health。
- `PROFILE_RUNTIME`。
- `examples/runtime_basic`。

验收：

- LittleFS 策略。
- Watchdog。
- Sleep。
- Health tick。

### Phase 3: Network

实现：

- WiFi。
- DNS。
- NTP。
- mDNS。
- `PROFILE_NET`。
- `PROFILE_NET_RUNTIME`。
- `examples/wifi_basic`。
- `examples/net_runtime`。

验收：

- AP 配网。
- Captive DNS。
- STA 重连。
- NTP/mDNS。
- WiFi event 入队。

### Phase 4: Web

实现：

- Web。
- WebStatus。
- WebWiFi。
- WebUtil。
- JSON helpers。
- `PROFILE_WEB`。
- `PROFILE_WEB_RUNTIME`。
- `examples/web_config`。
- `examples/web_runtime`。

验收：

- Auth。
- 状态 API。
- WiFi 配置 API。
- 自定义路由。
- JSON escape。

### Phase 5: Update

实现：

- Ota。
- WebOta。
- SHA256。
- Watchdog 联动。
- Config pause。
- mark valid / rollback。
- `PROFILE_FULL`。
- `examples/web_ota`。
- `examples/full_diagnostics`。

验收：

- 未授权拒绝。
- 未显式 `setAuth()` 拒绝 OTA。
- SHA256 正确/错误。
- 中途断电。
- 回滚。
- brownout。

### Phase 6: 文档和发布冻结

完成：

- README。
- docs。
- API 文档。
- Profile 表。
- 资源表。
- 实机诊断清单。
- 分区建议。
- 发布前 checklist。

发布冻结后：

- 不再做大规模架构调整。
- 只接受 bug fix、兼容性修复、文档修正、测试补充。

## 20. 文档结构

建议：

- `docs/00_design.md`
- `docs/01_architecture.md`
- `docs/02_profiles.md`
- `docs/03_api.md`
- `docs/04_web.md`
- `docs/05_ota.md`
- `docs/06_memory_budget.md`
- `docs/07_diagnostics.md`
- `docs/08_arduino_core_compat.md`
- `docs/09_release_checklist.md`

README 第一屏必须说明：

- 默认 profile 是 CORE。
- 如何选择 profile。
- 每个 profile 启用哪些模块。
- 各 profile 的资源差异。
- Basic Auth 安全边界。
- OTA 生产要求。
- 支持 Arduino Core 版本。

## 21. 明确不做

- 不支持 ESP8266。
- 不保留跨芯片 HAL。
- 不兼容旧 `EspBase` API。
- 不兼容当前 `Esp32Base` API。
- 不做 MQTT。
- 不做 HTTP Client。
- 不做 WebSocket。
- 不做复杂 Scheduler。
- 不做文件日志。
- 不做多用户权限。
- 不做 HTTPS。
- 不做大型前端 SPA。
- 不做云平台绑定。
- 不做静态模块注册表。
- 不做模板 policy 架构。

## 22. 最终 Profile 表

| Profile | Core | Bus | Runtime | WiFi | DNS | NTP | mDNS | Web | OTA | 用途 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| CORE | yes | no | no | no | no | no | no | no | no | 极简配置和诊断 |
| RUNTIME | yes | yes | yes | no | no | no | no | no | no | 离线本地设备 |
| NET | yes | no, 可显式开启 | no | yes | yes | yes | yes | no | no | 纯联网设备 |
| NET_RUNTIME | yes | yes | yes | yes | yes | yes | yes | no | no | 量产联网节点 |
| WEB | yes | yes, 可显式关闭 | no | yes | yes | yes | yes | yes | no | Web 配置和状态 |
| WEB_RUNTIME | yes | yes | yes | yes | yes | yes | yes | yes | no | Web + 本地可靠运行 |
| FULL | yes | yes | yes | yes | yes | yes | yes | yes | yes | 完整管理和 OTA |

## 23. 最终设计结论

1. Core 保持最小，不包含 Bus、WiFi、Web、OTA、Fs、Watchdog。
2. Profile 固定为 7 个，覆盖主要真实项目，不再继续扩张。
3. 用户可用 `ESP32BASE_ENABLE_*` 精细覆盖，但依赖检查必须严格。
4. Web 可在 STA connected 或 AP config portal 两种状态下启动。
5. Captive Portal DNS 是正式模块，不是后续优化。
6. OTA 必须包含认证 flag、SHA256、Watchdog 联动、Config pause、rollback。
7. LittleFS 默认不格式化，失败不 halt。
8. Config 必须 readonly 读取、长度校验、deferred delay、flushAll。
9. ESP32-C3 差异是正式设计约束。
10. Arduino Core 2.x / 3.x 兼容是正式测试矩阵。
11. CI 从 Phase 1 开始建立，资源表是发布要求。
12. 发布后只做维护，不再进行大规模架构重构。

## 24. V3 评估提示词

```text
请以资深嵌入式系统架构师、ESP32 Arduino Core 库维护者、长期量产设备固件负责人三重视角，严谨评估这份 Esp32Base 架构重构方案 V3。

特别注意：本项目目标是一次构建一个成熟好用的基础库，发布后不再进行大规模架构迭代，只做 bug 修复、兼容性维护、文档修正和必要小范围适配。

请重点评估：

1. V3 是否已经吸收 V2 评审的 9 个问题：
   - NET / WEB profile 中 Bus 语义是否一致
   - Profile 与 ENABLE_* 展开顺序是否可实现
   - OTA 是否使用 setAuth flag 而不是默认密码字符串判断
   - OTA 期间是否暂停 Config deferred flush
   - OTA 是否支持 SHA256 校验
   - 是否增加 WEB_RUNTIME profile
   - Phase 0 是否加入 Captive Portal 多设备验证
   - Health 与 Bus 默认容量是否明确
   - begin 失败策略是否清晰

2. V3 是否适合一次定型：
   - 是否还留下必须未来大改的架构债
   - profile 数量是否最终合适
   - 模块边界是否稳定
   - 公开 API 是否足够成熟

3. 资源裁剪是否可信：
   - CORE 是否真正不链接 WiFi/WebServer/Update/LittleFS
   - NET 是否不链接 WebServer/Update
   - WEB 是否不链接 Update
   - 关闭 Bus/Fs/Health 是否不会留下静态对象

4. 量产可靠性是否完整：
   - OTA 认证、SHA256、回滚、断电、Watchdog、NVS pause 是否闭环
   - Captive Portal 是否足够明确
   - NVS、LittleFS、mDNS、WiFi 重连是否有测试覆盖
   - ESP32-C3 与 Arduino Core 2.x/3.x 是否充分考虑

5. 实施阶段是否合理：
   - Phase 0 是否必要且充分
   - Phase 1~6 是否能持续保持可编译可验证
   - CI 和资源表是否启动得足够早

请输出：
- 总体结论：是否推荐 V3 作为最终实施蓝图
- 仍需修改的阻塞问题
- 可接受但需注意的风险
- 推荐最终模块顺序
- 推荐最终 profile 集合
- 最终实施阶段建议
- 发布前必须通过的实机验证清单
- 若认为仍需替代架构，请给出替代方案；否则明确说明不建议再推翻架构
```
