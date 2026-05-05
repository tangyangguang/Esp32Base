# Esp32Base 架构重构方案 V2

## 1. 方案定位

本方案是 `ESP32BASE_ARCHITECTURE_REDESIGN_PLAN.md` 的第二版，已吸收 `ESP32BASE_ARCHITECTURE_REDESIGN_REVIEW.md` 中的关键评估意见。

目标是把 `Esp32Base` 重构为 **ESP32 专用、模块顺序清晰、资源可裁剪、适合真实设备长期维护和量产验证** 的基础库。

本方案允许：

- 破坏现有 API。
- 删除并重写现有源码。
- 重命名模块。
- 重写文档、示例和测试矩阵。

本方案不追求：

- 兼容旧 `EspBase` API。
- 兼容当前 `Esp32Base` API。
- 支持 ESP8266。
- 做成大型 IoT 平台。

核心目标：

- **最小核心**：极简项目不为 WiFi、Web、OTA、LittleFS、Bus 等能力付资源成本。
- **按场景启用**：用少量 profile 覆盖常见真实设备组合。
- **编译期裁剪**：关闭的模块不编译、不链接、不初始化。
- **依赖单向**：模块只能依赖低层，不能反向依赖。
- **启动非阻塞**：WiFi、NTP、mDNS、Web、OTA 全部状态机化和延迟启动。
- **量产可靠**：补齐 OTA 回滚、Watchdog 联动、NVS 写入、LittleFS 挂载、Arduino Core 版本差异等实机风险。

## 2. 总体分层

最终分层采用 5 层结构。依赖方向只能从高层指向低层。

```text
Application
  ↓
Esp32Base entry
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

Core 是永远启用的最小集合。

模块：

- `Esp32BaseLog`
- `Esp32BaseConfig`
- `Esp32BaseSystem`

职责：

- 串口日志和日志格式化。
- NVS 小配置。
- deferred config write。
- heap / flash / reset reason / uptime 基础诊断。
- 统一 restart 入口。
- OTA app valid 标记的基础封装。

Core 明确不包含：

- WiFi。
- Web。
- OTA。
- LittleFS。
- Watchdog。
- Sleep。
- Event Bus。

设计理由：

- `Esp32BaseBus` 固定订阅表会消耗静态 RAM，不应让 `PROFILE_CORE` 默认承担。
- CORE profile 的目标是极简应用，应做到无 AP、无 Web、无 FS 挂载、无事件表。

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
- Task Watchdog 封装。
- deep sleep / light sleep。
- LittleFS 文件能力。
- 周期性健康诊断。

约束：

- 只依赖 Core。
- 不依赖 Network / Web / Update。
- 关闭 Runtime 能力时不得链接 LittleFS、Watchdog 相关对象或事件总线静态表。

### 2.3 Layer 2: Network

Network 是联网能力层。

模块：

- `Esp32BaseWiFi`
- `Esp32BaseDns`
- `Esp32BaseNtp`
- `Esp32BaseMdns`

职责：

- WiFi STA 非阻塞连接。
- AP config portal。
- 重连 backoff。
- Captive Portal DNS 拦截。
- NTP 延迟启动和时间格式化。
- mDNS 延迟启动和 HTTP service 注册。

约束：

- 依赖 Core。
- 如果启用 Bus，则 WiFi 状态通过 Bus 发布；如果未启用 Bus，仍必须独立可用。
- `Ntp` 和 `Mdns` 仅在 STA connected 后启动。
- `Dns` 仅在 AP config portal 模式启动。

### 2.4 Layer 3: Web

Web 是局域网管理层。

模块：

- `Esp32BaseWeb`
- `Esp32BaseWebStatus`
- `Esp32BaseWebWiFi`

职责：

- WebServer。
- Basic Auth / 可选 Digest Auth。
- 路由注册。
- 响应输出。
- JSON escape。
- 状态 API。
- WiFi 配置页面和 API。

约束：

- 依赖 WiFi。
- Web 启动条件是：

```text
WiFi connected OR WiFi config_portal
```

也就是说，配置门户 AP 模式下 Web 必须可启动，否则用户无法访问配网页面。

安全说明：

- HTTP Basic Auth 只用于防误操作，不是强安全边界。
- OTA 路由必须要求用户显式设置非默认认证信息，否则拒绝启动。
- 文档必须明确 LAN 内 Basic Auth 密码可被嗅探。

### 2.5 Layer 4: Update

Update 是固件升级层。

模块：

- `Esp32BaseOta`
- `Esp32BaseWebOta`

职责：

- OTA 状态机。
- 固件写入。
- OTA progress / error / bytes 记录。
- Web OTA 页面和上传 API。
- OTA 双分区回滚配合。

约束：

- 依赖 WiFi + Web。
- OTA 上传必须经过 Web Auth。
- 未授权请求不能进入 flash 写入流程。
- OTA 期间必须处理 Watchdog、yield、低电压和回滚风险。

## 3. Profile 设计

Profile 用于覆盖真实设备的常见组合。底层仍保留精细 `ESP32BASE_ENABLE_*` 宏。

默认 profile：`ESP32BASE_PROFILE_CORE`。

### 3.1 ESP32BASE_PROFILE_CORE

启用：

- Log
- Config
- System

不启用：

- Bus
- WiFi
- Web
- OTA
- LittleFS
- Watchdog
- Sleep

适合：

- 极简控制器。
- 只需要 NVS 配置和基础诊断的应用。
- 不需要联网和文件系统的设备。

资源目标：

- 不链接 WiFi。
- 不链接 WebServer。
- 不链接 Update。
- 不链接 LittleFS。
- 不创建 AP。
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

- 无联网但需要本地文件、低功耗、看门狗、健康诊断的设备。

### 3.3 ESP32BASE_PROFILE_NET

启用：

- CORE
- WiFi
- Dns
- Ntp
- Mdns

适合：

- 联网但不需要 Web 管理页的设备。
- 业务自己使用 MQTT、HTTP Client 或其他通信库。

不启用：

- Web。
- OTA。
- Fs。
- Watchdog。
- Sleep。

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
- 传感器、执行器、数据采集节点。
- 业务层自带 MQTT / HTTP Client 的设备。

设计理由：

- 这是实际项目中非常常见的组合。
- 不应迫使用户升到 WEB / FULL profile 才获得 Watchdog 或 Fs。

### 3.5 ESP32BASE_PROFILE_WEB

启用：

- NET
- Web
- WebStatus
- WebWiFi
- Bus

适合：

- 需要局域网配置和状态查看的设备。
- 不需要 OTA 的 Web 管理型应用。

不启用：

- OTA。
- Fs。
- Watchdog。
- Sleep。

### 3.6 ESP32BASE_PROFILE_FULL

启用：

- WEB
- OTA
- Fs
- Watchdog
- Sleep
- Health

适合：

- 完整本地管理。
- Web OTA。
- 文件能力。
- 看门狗。
- 低功耗。
- 诊断压测。

## 4. 底层编译宏

底层能力宏：

- `ESP32BASE_ENABLE_LOG`
- `ESP32BASE_ENABLE_CONFIG`
- `ESP32BASE_ENABLE_SYSTEM`
- `ESP32BASE_ENABLE_BUS`
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
- `ESP32BASE_ENABLE_FS`
- `ESP32BASE_ENABLE_WATCHDOG`
- `ESP32BASE_ENABLE_SLEEP`

规则：

- Profile 先展开。
- 用户显式设置的 `ESP32BASE_ENABLE_*` 可覆盖 profile 默认值。
- 依赖检查在 `Esp32BaseProfile.h` 中完成。
- 无效组合必须编译时报错。

依赖检查：

- `WEB` 需要 `WIFI`。
- `WEB_WIFI` 需要 `WEB` + `WIFI`.
- `WEB_STATUS` 需要 `WEB`。
- `WEB_OTA` 需要 `WEB` + `OTA`。
- `OTA` 需要 `WIFI`。
- `NTP` 需要 `WIFI`。
- `MDNS` 需要 `WIFI`。
- `DNS` 需要 `WIFI`。
- `HEALTH` 建议需要 `BUS`，如果未启用 Bus，则只保留本地查询 API，不发布事件。

关闭模块的要求：

- 对应 `.cpp` 整体用 `#if ESP32BASE_ENABLE_XXX` 包裹。
- 关闭后不得产生全局对象。
- 关闭后不得链接对应 Arduino 库。
- 关闭后 `Esp32Base::begin()` / `handle()` 中不得调用该模块。

## 5. 公开 API 形态

采用静态类 API，名称直接表达模块职责。

### 5.1 聚合头

提供：

- `Esp32Base.h`
- `Esp32BaseAll.h`

`Esp32Base.h`：

- 包含 profile 配置。
- 包含 Core API。
- 按 `ESP32BASE_ENABLE_*` 条件包含启用模块。

`Esp32BaseAll.h`：

- 面向示例和调试。
- 聚合所有模块头，但模块实现仍按宏裁剪。

### 5.2 公开模块

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

内部模块不建议直接公开：

- `Esp32BaseWebStatus`
- `Esp32BaseWebWiFi`
- `Esp32BaseWebOta`

这些模块负责内置路由注册，用户只通过 `Esp32BaseWeb` 和 `Esp32BaseOta` 交互。

### 5.3 命名空间

V1 实施阶段保留 `Esp32Base*` 静态类命名，降低 Arduino 用户理解成本。

V2 演进可考虑引入：

```cpp
namespace esp32base {
    using WiFi = Esp32BaseWiFi;
    using Web = Esp32BaseWeb;
}
```

不在第一版重构中强制切换命名空间，避免同时引入过多 API 风格变化。

## 6. 统一入口职责

`Esp32Base` 负责：

- 固件信息。
- hostname。
- profile 字符串。
- begin / handle。
- 延迟启动。
- 启动诊断。
- 资源日志。

`Esp32Base` 不负责：

- WiFi 凭证细节。
- Web 路由实现。
- OTA 写入细节。
- 文件 API。
- Watchdog 具体配置。
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

`begin()` 不等待：

- WiFi 连接。
- NTP 同步。
- mDNS 启动。
- Web 启动。
- OTA ready。

### 6.2 handle 顺序

`Esp32Base::handle()` 顺序：

1. Config deferred flush，一轮最多一条。
2. WiFi handle，如启用。
3. Captive DNS handle，如启用且处于 config portal。
4. 条件启动 Web：WiFi connected 或 WiFi config portal。
5. 条件启动 NTP：WiFi connected。
6. 条件启动 mDNS：WiFi connected。
7. 条件启动 OTA：WiFi connected + Web ready + OTA enabled。
8. Web handle，如 ready。
9. OTA handle，如 ready。
10. Health handle，如启用。
11. Watchdog feed，如启用。
12. Fs maintenance，如需要。
13. 一次性启动诊断日志。

设计取舍：

- 第一版重构使用硬编码顺序 + `#if ESP32BASE_ENABLE_*`。
- 新增模块需要修改 `Esp32Base.cpp`。
- 这是有意选择，优先简单、可读、可调试。
- 静态注册表作为后续演进方向，不在第一版引入。

### 6.3 restart / sleep 统一路径

所有重启必须走：

```cpp
Esp32BaseSystem::restart(const char* reason)
```

所有 deep sleep 必须走：

```cpp
Esp32BaseSleep::deepSleep(...)
```

统一流程：

1. 发布 `system.restart` 或 `system.sleep` 事件，如 Bus 启用。
2. `Esp32BaseConfig::flushAll()`。
3. 输出最后诊断日志。
4. 处理 Watchdog。
5. 执行 `esp_restart()` 或 `esp_deep_sleep_start()`。

禁止模块内部直接调用：

- `ESP.restart()`
- `esp_restart()`
- `esp_deep_sleep_start()`

CI 中增加文本检查，避免绕过统一生命周期路径。

## 7. 事件总线设计

`Esp32BaseBus` 是 Runtime 可选模块，不属于 Core 默认能力。

### 7.1 语义

- 同步发布。
- 固定容量订阅表。
- 不面向 ISR。
- 不作为任务队列。
- 不作为复杂 async framework。

### 7.2 多任务规则

所有 `publish()` 必须发生在 Arduino `loop()` 任务。

原因：

- WiFi event callback 可能运行在 LWIP / system event 任务。
- 直接跨任务同步发布会引入锁、死锁和不可预测阻塞。

模块内部规则：

- ISR / LWIP / event task 回调只记录轻量状态或入固定队列。
- 实际 Bus publish 在模块 `handle()` 中执行。

第一版不使用 `portMUX_TYPE` 跨任务 publish。

### 7.3 事件常量

事件名必须由模块导出常量，避免用户硬编码字符串。

示例：

```cpp
class Esp32BaseWiFi {
public:
    static constexpr const char* EVENT_CONNECTED = "wifi.connected";
    static constexpr const char* EVENT_DISCONNECTED = "wifi.disconnected";
    static constexpr const char* EVENT_CONFIG_PORTAL = "wifi.config_portal";
};
```

建议事件：

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

## 8. Core 细节

### 8.1 Log

要求：

- 编译期日志级别过滤。
- Release 中关闭的日志级别不得保留 format 字符串到 `.rodata`。
- 默认串口波特率可配置。
- 支持字节数、时长、uptime 格式化。

### 8.2 Config

后端：

- ESP32 NVS。
- 不保留跨芯片 HAL。

规则：

- namespace 长度必须 `1..15`。
- key 长度必须 `1..15`。
- 非法 namespace/key 返回 false，并输出 debug/warn 日志。
- 读取使用 readonly NVS，不因读取创建 namespace。
- 写入前比较旧值，无变化不写。
- deferred write 支持 delay。
- `handle()` 每轮最多写一条到期 pending。
- `flushAll()` 忽略 delay，写完所有 pending。
- `clearNamespace()` 清 NVS namespace，同时清该 namespace pending。

建议 API：

```cpp
bool setStr(const char* ns, const char* key, const char* value);
bool getStr(const char* ns, const char* key, char* out, size_t len, const char* def = "");
bool setInt(const char* ns, const char* key, int32_t value);
bool setIntDeferred(const char* ns, const char* key, int32_t value, uint32_t delayMs = 1000);
bool setBool(const char* ns, const char* key, bool value);
bool setBoolDeferred(const char* ns, const char* key, bool value, uint32_t delayMs = 1000);
bool flushOne();
bool flushAll();
```

对于常量 namespace/key，可提供宏辅助做编译期检查：

```cpp
ESP32BASE_CONFIG_STATIC_NAME("wifi_ssid")
```

### 8.3 System

职责：

- heap。
- min free heap。
- flash size。
- reset reason。
- wake reason 查询。
- uptime。
- unified restart。
- OTA app valid 标记基础封装。

OTA 相关基础 API：

```cpp
bool markCurrentAppValid();
bool isRollbackPossible();
void rollbackAndRestart(const char* reason);
```

注意：

- 具体 OTA 状态机仍属于 `Esp32BaseOta`。
- `System` 只封装 ESP-IDF 生命周期能力。

## 9. Runtime 细节

### 9.1 Watchdog

要求：

- 默认不启用。
- 由 profile 或用户显式开启。
- 支持 Arduino Core 2.x / 3.x 的 TWDT API 差异。
- 启用后由 `Esp32Base::handle()` 自动 feed。
- OTA 长任务可临时 remove current task，结束后 restore。

API：

```cpp
bool begin(uint32_t timeoutMs);
void feed();
bool removeCurrentTaskForLongOperation();
bool restoreCurrentTaskAfterLongOperation();
bool wasWatchdogReset();
uint32_t resetCount();
```

### 9.2 Sleep

要求：

- deep sleep 支持 timer wake。
- light sleep 作为可选能力。
- wake on GPIO 按芯片能力差异处理。

芯片差异：

- ESP32 / ESP32-S3：支持更完整 RTC GPIO wake。
- ESP32-C3：GPIO wake 能力受限，不提供不可实现的统一承诺。

API 必须返回 bool 或错误码，不能在不支持芯片上静默成功。

### 9.3 Fs

默认文件系统：

- LittleFS。

策略：

1. `LittleFS.begin(false)` 尝试挂载。
2. 挂载失败且 `ESP32BASE_FS_AUTO_FORMAT=1` 时才格式化。
3. 挂载失败且未开启 auto-format 时，记录错误并继续启动，不 halt。
4. 首次成功挂载后，可在 NVS 写入 `fs.formatted=1` 诊断标志。
5. 格式化必须有明确日志。

默认：

```cpp
ESP32BASE_FS_AUTO_FORMAT=0
```

### 9.4 Health

可选模块，用于运行健康诊断。

职责：

- 周期性记录 uptime。
- free heap。
- min free heap。
- loop 周期。
- reset reason。
- WiFi 状态，如启用 WiFi。

如果 Bus 启用：

- 发布 `health.tick`。

如果 Bus 未启用：

- 只提供查询 API。

## 10. Network 细节

### 10.1 WiFi

状态：

- `IDLE`
- `CONNECTING`
- `CONNECTED`
- `CONFIG_PORTAL`
- `RETRY_BACKOFF`
- `FAILED`

规则：

- `begin()` 不阻塞。
- 无保存凭证时进入 AP config portal。
- 有凭证时启动 STA 连接。
- 连接超时后进入 backoff。
- backoff 默认三档：
  - 前 5 次：15s
  - 6 到 10 次：60s
  - 10 次后：300s
- 支持最大重试次数，默认无限重试。
- 支持 power save 配置。

WiFi event callback：

- 只更新轻量状态。
- 不直接 publish Bus。
- 不直接写 NVS。
- 不执行重操作。

### 10.2 Captive DNS

模块：

- `Esp32BaseDns`

职责：

- AP config portal 模式启动 DNSServer。
- 拦截常见域名解析到 AP IP。
- 退出 config portal 时停止 DNS。

Web 配网页面依赖：

- WiFi 进入 config portal。
- DNS 启动。
- Web 在 config portal 条件下启动。

### 10.3 NTP

规则：

- 仅 WiFi connected 后启动。
- 不阻塞等待同步。
- 支持 1 到 3 个 server。
- 支持时区配置由应用自行处理，库默认 UTC。
- `formatTime()` 在未同步时返回 false。

### 10.4 mDNS

规则：

- 仅 WiFi connected 后启动。
- hostname 来自 `Esp32Base::setHostname()`。
- Web 启用时注册 HTTP service。
- WiFi 断开时停止或标记不运行，重连后可恢复。

## 11. Web 细节

### 11.1 WebServer

采用 Arduino `WebServer`，不引入 AsyncWebServer。

理由：

- 依赖少。
- 行为可控。
- 适合小型管理页。
- 和当前项目边界一致。

### 11.2 路由

规则：

- 固定容量路由表。
- 应用路由可在 `Esp32Base::begin()` 前或后注册。
- 内置路由和应用路由共享认证边界。

默认容量：

- ESP32 / ESP32-S3：16。
- ESP32-C3：建议 8 或 12，按实测确定。

### 11.3 认证

默认：

- Web Auth 开启。
- 默认开发账号密码仍可存在，但 OTA 不允许使用默认密码。

规则：

- `Esp32BaseWeb::setAuth()` 标记 auth 已由应用显式设置。
- OTA 路由启动前检查：
  - Auth enabled。
  - Auth credentials 已显式设置。
  - 密码不是默认值。
- 不满足则 OTA route 不注册，并输出 ERROR/WARN。

安全文档必须写明：

- HTTP Basic Auth 明文传输。
- 它用于防误操作，不用于抵御主动攻击。
- 生产环境必须改默认密码。

可选：

- 后续支持 Digest Auth。
- 第一版不强制实现 HTTPS。

### 11.4 JSON 输出

必须提供：

```cpp
void beginJson(int code);
void writeJsonEscaped(const char* text);
void endJson();
void sendJson(int code, const char* json);
```

所有内置 JSON 中的字符串字段必须 escape：

- `"`
- `\`
- `\n`
- `\r`
- `\t`
- 控制字符 `< 0x20`

避免大 String 拼接。

### 11.5 内置路由

建议内置路径：

- `GET /esp32base`
- `GET /esp32base/api/status`
- `GET /esp32base/api/chip`
- `GET /esp32base/api/firmware`
- `GET /esp32base/wifi`
- `POST /esp32base/wifi`
- `POST /esp32base/api/wifi`
- `POST /esp32base/api/wifi/clear`

OTA 路由由 `Esp32BaseWebOta` 注册：

- `GET /esp32base/ota`
- `POST /esp32base/ota`
- `GET /esp32base/api/ota`

## 12. OTA 细节

### 12.1 状态机

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
- lastError。
- startTime。

### 12.2 Web OTA 上传协议

规则：

- 上传开始前必须认证成功。
- 未授权请求不得调用 `Update.begin()`。
- 上传期间每个 chunk 后调用 `yield()`。
- 写入失败立即进入 FAILED。
- 结束时 `Update.end(true)` 成功后进入 SUCCESS。
- 成功后走统一 restart。

### 12.3 Watchdog 联动

如果 Watchdog 启用：

1. OTA 写入前调用 `Esp32BaseWatchdog::removeCurrentTaskForLongOperation()`。
2. 每个 chunk 后 `yield()`。
3. OTA 完成、失败、abort 后调用 `restoreCurrentTaskAfterLongOperation()`。
4. 任何失败路径都必须恢复 Watchdog 状态。

### 12.4 Brownout 策略

OTA 写 flash 时可能遇到低电压 brownout。

第一版策略：

- 默认不关闭 brownout detector。
- 提供可选宏：

```cpp
ESP32BASE_OTA_DISABLE_BROWNOUT_DURING_WRITE=0
```

- 只有用户显式开启时，OTA 写入期间临时关闭 brownout，结束后恢复。
- 文档必须说明风险：关闭 brownout 可能掩盖供电问题，不建议默认开启。

### 12.5 OTA 回滚

支持 ESP32 双 OTA 分区回滚。

API：

```cpp
bool Esp32BaseOta::markCurrentValid();
bool Esp32BaseOta::isRollbackPossible();
void Esp32BaseOta::rollbackAndRestart(const char* reason);
```

启动策略：

- 新固件启动后，应用应在自检通过后调用 `markCurrentValid()`。
- 可选启用 boot validation timeout：

```cpp
ESP32BASE_OTA_REQUIRE_MARK_VALID=0
ESP32BASE_OTA_MARK_VALID_TIMEOUT_MS=30000
```

当 `REQUIRE_MARK_VALID=1` 且超时未标记：

- 记录日志。
- 调用 rollback and restart。

第一版至少实现手动 `markCurrentValid()` 和 rollback 查询。
自动超时回滚可作为 FULL profile 的可选能力。

## 13. Arduino Core 与芯片差异

### 13.1 支持范围

建议支持：

- Arduino ESP32 Core 2.0.14 及以上。
- Arduino ESP32 Core 3.0.4 及以上。

必须通过条件编译处理：

- `ESP_ARDUINO_VERSION_MAJOR`
- `ESP_ARDUINO_VERSION_MINOR`
- `CONFIG_IDF_TARGET_ESP32`
- `CONFIG_IDF_TARGET_ESP32S3`
- `CONFIG_IDF_TARGET_ESP32C3`

### 13.2 已知差异

Watchdog：

- Arduino Core 2.x：`esp_task_wdt_init(timeout, panic)`。
- Arduino Core 3.x：`esp_task_wdt_init(const esp_task_wdt_config_t*)`。

WiFi event：

- Arduino Core 2.x：`SYSTEM_EVENT_*`。
- Arduino Core 3.x：`ARDUINO_EVENT_*`。

Sleep：

- ESP32 / ESP32-S3 RTC GPIO 能力较完整。
- ESP32-C3 wake source 受限。

PSRAM：

- 第一版不依赖 PSRAM。
- 如未来使用 PSRAM，必须检查 `BOARD_HAS_PSRAM` 和相关配置。

缓冲默认值：

- ESP32-C3 默认 route/page/pending 容量可低于 ESP32/S3。
- 所有容量必须可通过宏覆盖。

## 14. 目录结构

建议重建：

```text
src/
├── Esp32Base.h
├── Esp32BaseAll.h
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

## 15. 示例项目

示例按 profile 组织：

- `examples/core_basic`
  - `ESP32BASE_PROFILE_CORE`
- `examples/runtime_basic`
  - `ESP32BASE_PROFILE_RUNTIME`
- `examples/wifi_basic`
  - `ESP32BASE_PROFILE_NET`
- `examples/net_runtime`
  - `ESP32BASE_PROFILE_NET_RUNTIME`
- `examples/web_config`
  - `ESP32BASE_PROFILE_WEB`
- `examples/web_ota`
  - `ESP32BASE_PROFILE_FULL`
- `examples/full_diagnostics`
  - `ESP32BASE_PROFILE_FULL`

每个示例包含：

- ESP32 env。
- ESP32-S3 env。
- ESP32-C3 env。

示例必须避免：

- 在 CORE 示例中 include WiFi/Web/FS/OTA。
- 在 NET 示例中 include Web/OTA。
- 在 WEB 示例中 include Update/OTA。

## 16. 测试计划

### 16.1 原型验证

实施前必须先做 4 个原型：

1. Profile 资源裁剪原型：
   - 6 个空 main。
   - 分别启用 6 个 profile。
   - 检查 map 文件，确认 CORE 不链接 WiFi/WebServer/Update/LittleFS。
2. Arduino Core 2.x / 3.x 编译原型：
   - 同一份最小代码在 2.0.14 与 3.0.4 编译。
   - 覆盖 ESP32 / ESP32-S3 / ESP32-C3。
3. OTA + Watchdog 原型：
   - 上传大固件。
   - 验证 remove/restore task。
   - 验证失败路径恢复 Watchdog。
4. WiFi event 入队原型：
   - 验证 WiFi callback 只入队。
   - loop 中 publish。
   - 观察事件延迟和丢失。

### 16.2 编译验证

矩阵：

- 6 个 profile。
- ESP32。
- ESP32-S3。
- ESP32-C3。
- Arduino Core 2.x。
- Arduino Core 3.x。

目标：

- CORE 不链接 WiFi、WebServer、Update、LittleFS。
- NET 不链接 WebServer、Update。
- WEB 不链接 Update。
- FULL 全部链接并通过容量检查。

### 16.3 实机验证

必须覆盖：

- CORE：无 AP、无 Web、无 FS 挂载。
- RUNTIME：Watchdog、Sleep、LittleFS 文件读写。
- NET：无凭证进入 AP 配网，有凭证 STA 非阻塞连接。
- NET_RUNTIME：WiFi + Watchdog + Fs 长时间运行。
- WEB：Basic Auth、状态 API、WiFi 配置 API、自定义路由。
- FULL：OTA 成功、未授权 OTA 被拒绝、OTA 期间 Watchdog 安全。
- Captive Portal：手机连 AP 后 DNS 拦截生效。
- OTA 中途断电：30% / 70% / 99% 三个点位断电，确认不变砖。
- OTA 后未 markValid：确认可回滚。
- 路由器掉电：断网 5 分钟后恢复，确认重连和服务恢复。
- NVS 写满：写入返回 false，不崩溃、不死循环。
- LittleFS 首次挂载：不开 auto-format 时失败不 halt。
- JSON escape：SSID / hostname 含特殊字符时输出合法 JSON。
- ESP32-C3 单核重负载：WiFi + Web + Fs 同时运行，Watchdog 不误触发。
- deferred write + restart：pending 队列非空时 restart，确认全部落盘。

### 16.4 资源验证

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

建议 CI 自动生成资源表，写入文档或 release notes。

## 17. 实施阶段

不采用一次性大爆炸式提交。即使允许重写，也按阶段推进，每个阶段都保持可编译、可烧录、可验证。

### Phase 0: 原型验证

目标：

- 验证 profile 裁剪。
- 验证 Arduino Core 2/3 差异。
- 验证 OTA + Watchdog。
- 验证 WiFi event 入队。

产物：

- 原型代码。
- 资源测量表。
- 兼容性差异表。

### Phase 1: Core

实现：

- `Esp32BaseProfile.h`
- `Esp32BaseLog`
- `Esp32BaseConfig`
- `Esp32BaseSystem`
- `Esp32Base`
- `examples/core_basic`

验证：

- CORE 三芯片编译。
- 不链接 WiFi/Web/Update/LittleFS。

### Phase 2: Runtime

实现：

- `Esp32BaseBus`
- `Esp32BaseWatchdog`
- `Esp32BaseSleep`
- `Esp32BaseFs`
- `Esp32BaseHealth`
- `PROFILE_RUNTIME`
- `examples/runtime_basic`

验证：

- LittleFS 挂载策略。
- Watchdog enable/feed/reset count。
- Sleep wake reason。

### Phase 3: Network

实现：

- `Esp32BaseWiFi`
- `Esp32BaseDns`
- `Esp32BaseNtp`
- `Esp32BaseMdns`
- `PROFILE_NET`
- `PROFILE_NET_RUNTIME`
- `examples/wifi_basic`
- `examples/net_runtime`

验证：

- AP 配网。
- Captive DNS。
- STA 重连。
- NTP/mDNS 延迟启动。

### Phase 4: Web

实现：

- `Esp32BaseWeb`
- `Esp32BaseWebStatus`
- `Esp32BaseWebWiFi`
- JSON helpers
- `PROFILE_WEB`
- `examples/web_config`

验证：

- Auth。
- 状态 API。
- WiFi 配置 API。
- 自定义路由。
- JSON escape。

### Phase 5: Update

实现：

- `Esp32BaseOta`
- `Esp32BaseWebOta`
- OTA + Watchdog 协议。
- OTA mark valid / rollback 基础能力。
- `PROFILE_FULL`
- `examples/web_ota`
- `examples/full_diagnostics`

验证：

- Web OTA。
- 未授权拒绝。
- 默认密码拒绝启动 OTA。
- 中途断电。
- 回滚。

### Phase 6: 文档和 CI 收尾

实现：

- README 重写。
- docs 全部按新架构重写。
- profile 使用说明。
- API 文档。
- 内存预算。
- 实机诊断清单。
- CI 编译矩阵。
- 资源表生成。

## 18. 文档结构

建议文档：

- `docs/00_design.md`
- `docs/01_architecture.md`
- `docs/02_profiles.md`
- `docs/03_api.md`
- `docs/04_web.md`
- `docs/05_ota.md`
- `docs/06_memory_budget.md`
- `docs/07_diagnostics.md`
- `docs/08_arduino_core_compat.md`

README 第一屏必须说明：

- 默认 profile 是 CORE。
- 如何选择 profile。
- 各 profile 启用哪些模块。
- 各 profile 的资源差异。
- Basic Auth 的安全边界。
- OTA 生产使用要求。

## 19. 明确不做

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

## 20. 关键设计结论

1. 采用 5 层结构是合理的，但 Core 必须更小，Bus 不默认进 Core。
2. Profile 必须增加 `NET_RUNTIME`，否则真实项目常见组合覆盖不足。
3. Web 必须在 `WiFi connected` 或 `WiFi config_portal` 两种条件下启动。
4. Captive Portal DNS 是 AP 配网体验的必要组成。
5. Arduino Core 2.x / 3.x 差异必须作为一等设计约束。
6. OTA 不是简单 Web 上传，必须包含认证、Watchdog、yield、失败恢复、回滚。
7. LittleFS 默认不格式化，挂载失败不 halt。
8. Config 必须完整处理 NVS 长度限制、readonly 读取、flushAll。
9. ESP32-C3 单核和 wake source 限制必须进入 API 和测试。
10. 第一版保持硬编码启动顺序是合理工程取舍；静态注册表留作未来演进。

## 21. 附：推荐 profile 表

| Profile | Core | Bus | Runtime | WiFi | DNS | NTP | mDNS | Web | OTA | 典型用途 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| CORE | yes | no | no | no | no | no | no | no | no | 极简配置和诊断 |
| RUNTIME | yes | yes | yes | no | no | no | no | no | no | 本地文件、看门狗、低功耗 |
| NET | yes | optional | no | yes | yes | yes | yes | no | no | 纯联网设备 |
| NET_RUNTIME | yes | yes | yes | yes | yes | yes | yes | no | no | 量产联网节点 |
| WEB | yes | yes | no | yes | yes | yes | yes | yes | no | Web 配置和状态 |
| FULL | yes | yes | yes | yes | yes | yes | yes | yes | yes | 完整管理和 OTA |

## 22. 附：方案评估提示词 V2

```text
请以资深嵌入式系统架构师、ESP32 Arduino Core 库维护者、长期量产设备固件负责人三重视角，严谨评估这份 Esp32Base 架构重构方案 V2。

请重点判断：

1. V2 是否已经解决 V1 评审中的 12 个必改点：
   - Bus 不默认进 Core
   - 增加 NET_RUNTIME profile
   - Web 在 connected/config_portal 均可启动
   - Arduino Core 2.x/3.x 兼容策略
   - OTA + Watchdog 协议
   - Bus loop-task-only 发布语义
   - NVS namespace/key 长度限制
   - LittleFS 首次挂载策略
   - ESP32-C3 差异
   - OTA 回滚
   - OTA 默认密码拒绝
   - Captive Portal DNS

2. V2 的分层是否仍然清晰：
   - Core 是否足够小？
   - Runtime / Network / Web / Update 是否职责明确？
   - 是否存在隐藏依赖或循环依赖？

3. V2 的 profile 是否合理：
   - CORE / RUNTIME / NET / NET_RUNTIME / WEB / FULL 是否覆盖主要真实项目？
   - 是否有 profile 过重或过轻？
   - Profile 与 ENABLE_* 覆盖规则是否清晰？

4. V2 是否真正能节省资源：
   - CORE 是否能不链接 WiFi/WebServer/Update/LittleFS？
   - NET 是否能不链接 WebServer/Update？
   - WEB 是否能不链接 Update？
   - Bus、Health、Fs 等是否会意外进入轻量 profile？

5. V2 的量产可靠性是否足够：
   - Config flushAll 是否闭环？
   - OTA 认证、回滚、断电、Watchdog 是否闭环？
   - LittleFS、NVS、WiFi 重连、Captive Portal 是否可靠？
   - Arduino Core 2.x/3.x 与 ESP32-C3 是否考虑充分？

6. V2 的实施计划是否可落地：
   - Phase 0 原型是否必要且充分？
   - Phase 1~6 顺序是否合理？
   - 是否有阶段风险遗漏？
   - 是否应该进一步压缩或拆分模块？

请输出：
- 总体结论：是否推荐采用 V2
- V2 相比 V1 的改进是否充分
- 仍然必须修改的问题
- 可选优化建议
- 推荐最终模块顺序
- 推荐最终 profile 集合
- 实施阶段风险评估
- 必须实机验证的场景
- 如果你认为还有更合理的替代架构，请给出完整替代方案
```
