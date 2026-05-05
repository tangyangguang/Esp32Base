# Esp32Base 架构重构方案

## 1. 方案摘要

把项目重构为 **ESP32 专用、分层清晰、按场景裁剪、模块有序启动** 的基础库。

本轮设计允许破坏现有 API，必要时可以删除并重写现有代码。目标不是兼容旧实现，而是得到长期更合理、更稳定、更节省资源的架构。

核心原则：

- 核心极小，永远可用。
- 能力模块按依赖顺序排列，不能反向依赖。
- 默认按 profile 启用少量典型场景，不再默认全功能。
- 所有重模块必须能从编译产物中裁掉。
- 统一入口负责调度，但不变成“所有模块强耦合中心”。

## 2. 分层架构

模块按依赖顺序分为 5 层。

### 2.1 Core

模块：

- `Esp32BaseLog`
- `Esp32BaseConfig`
- `Esp32BaseBus`
- `Esp32BaseSystem`

职责：

- 日志
- NVS 小配置
- 同步事件总线
- heap / flash / reset reason / restart 基础诊断

约束：

- 不依赖任何可选模块。
- 所有 profile 都启用。

### 2.2 Device Runtime

模块：

- `Esp32BaseWatchdog`
- `Esp32BaseSleep`
- `Esp32BaseFs`

职责：

- 看门狗
- deep sleep / light sleep
- LittleFS 文件能力

约束：

- 可依赖 Core。
- 不依赖 WiFi / Web / OTA。

### 2.3 Network

模块：

- `Esp32BaseWiFi`
- `Esp32BaseNtp`
- `Esp32BaseMdns`

职责：

- WiFi STA / AP 配网
- 非阻塞重连状态机
- NTP 对时
- mDNS

约束：

- `Ntp` / `Mdns` 依赖 `WiFi connected`。
- 由统一入口在依赖满足后延迟启动。

### 2.4 Web

模块：

- `Esp32BaseWeb`
- `Esp32BaseWebStatus`
- `Esp32BaseWebWiFi`

职责：

- WebServer
- Basic Auth
- 自定义路由
- 状态 API
- WiFi 配置页面/API

约束：

- 依赖 WiFi。
- 只有 WiFi connected 或 config portal 时才启动。

### 2.5 Update

模块：

- `Esp32BaseOta`
- `Esp32BaseWebOta`

职责：

- OTA 状态机
- 固件写入
- Web OTA 页面/API

约束：

- 依赖 WiFi + Web。
- 必须经过 Web Auth。

## 3. 统一入口

`Esp32Base` 作为统一入口，负责：

- 固件信息
- hostname
- profile 状态
- 模块启动顺序
- 延迟启动
- 主循环调度
- 启动诊断

`Esp32Base` 不直接吞并各模块职责。

### 3.1 begin 顺序

`Esp32Base::begin()` 固定顺序：

1. Log
2. Config
3. Bus
4. System
5. Fs
6. Watchdog
7. Sleep
8. WiFi
9. 标记 core ready

只初始化当前 profile 启用且可立即启动的模块。

不在 `begin()` 中阻塞等待：

- WiFi 连接
- NTP 同步
- mDNS 启动
- Web 启动
- OTA 启动

### 3.2 handle 顺序

`Esp32Base::handle()` 固定顺序：

1. Config deferred flush，一轮最多一条。
2. WiFi 状态机。
3. WiFi ready 后启动 Web。
4. WiFi connected 后启动 NTP。
5. WiFi connected 后启动 mDNS。
6. Web ready + WiFi connected 后启动 OTA。
7. Web handle。
8. OTA handle。
9. Watchdog feed。
10. Fs maintenance，如有。
11. 一次性启动诊断日志。

### 3.3 restart / sleep 前处理

重启和 sleep 前必须：

1. 调用 `Esp32BaseConfig::flushAll()`。
2. 停止或喂狗。
3. 输出最后诊断日志。
4. 执行 restart / deep sleep。

## 4. Profiles 与编译期开关

提供少量典型场景 profile。profile 展开为底层 `ESP32BASE_ENABLE_*` 宏。

### 4.1 ESP32BASE_PROFILE_CORE

启用：

- Log
- Config
- Bus
- System

适合：

- 只需要配置、诊断、重启的极简应用。

### 4.2 ESP32BASE_PROFILE_RUNTIME

启用：

- CORE
- Watchdog
- Sleep
- Fs

适合：

- 无联网但需要本地文件、低功耗或可靠运行的设备。

### 4.3 ESP32BASE_PROFILE_NET

启用：

- CORE
- WiFi
- NTP
- mDNS

适合：

- 联网但不需要 Web 管理页的设备。

### 4.4 ESP32BASE_PROFILE_WEB

启用：

- NET
- Web
- Status API
- WiFi 配网页面

适合：

- 需要局域网配置和状态查看的设备。

### 4.5 ESP32BASE_PROFILE_FULL

启用：

- WEB
- OTA
- Fs
- Watchdog
- Sleep

适合：

- 完整管理、OTA、文件与可靠性诊断。

### 4.6 底层宏

底层能力宏：

- `ESP32BASE_ENABLE_LOG`
- `ESP32BASE_ENABLE_CONFIG`
- `ESP32BASE_ENABLE_BUS`
- `ESP32BASE_ENABLE_SYSTEM`
- `ESP32BASE_ENABLE_WIFI`
- `ESP32BASE_ENABLE_NTP`
- `ESP32BASE_ENABLE_MDNS`
- `ESP32BASE_ENABLE_WEB`
- `ESP32BASE_ENABLE_OTA`
- `ESP32BASE_ENABLE_FS`
- `ESP32BASE_ENABLE_WATCHDOG`
- `ESP32BASE_ENABLE_SLEEP`

默认 profile：`CORE`。

所有模块 `.cpp` 必须用对应宏包裹，确保关闭后：

- 不编译
- 不链接
- 不初始化

## 5. 公开 API

公开类按模块直接命名，不再强行塞进 5 个大类：

- `Esp32Base`：统一入口、profile 状态、固件信息、hostname。
- `Esp32BaseLog`：日志和格式化工具。
- `Esp32BaseConfig`：NVS KV、deferred write、flush all、namespace 清理。
- `Esp32BaseBus`：同步事件总线。
- `Esp32BaseSystem`：heap / flash / reset / restart 基础系统信息。
- `Esp32BaseWatchdog`：看门狗。
- `Esp32BaseSleep`：sleep。
- `Esp32BaseFs`：LittleFS。
- `Esp32BaseWiFi`：WiFi 状态机和配置。
- `Esp32BaseNtp`：NTP。
- `Esp32BaseMdns`：mDNS。
- `Esp32BaseWeb`：WebServer、认证、路由、响应工具。
- `Esp32BaseOta`：OTA 状态机和固件写入。

## 6. 事件总线

模块间通信优先使用 `Esp32BaseBus`。

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

事件总线要求：

- 同步发布。
- 固定容量订阅表。
- 不面向 ISR。
- 不作为复杂异步框架。

## 7. 实施调整点

### 7.1 目录结构

重建 `src/` 目录结构：

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

### 7.2 职责拆分

删除旧的“大类混合职责”实现：

- Runtime 不再包含 LittleFS 和 Watchdog。
- Network 不再混装 Web / OTA 逻辑。
- Web 不再内置 OTA 写入逻辑。

### 7.3 Profile 处理

新增 `Esp32BaseProfile.h`：

- 负责 profile 到 `ESP32BASE_ENABLE_*` 的宏展开。
- 做依赖检查，例如 OTA 必须启用 Web 和 WiFi。
- 输出编译期错误，避免无效组合静默通过。

### 7.4 Web 输出

Web 输出统一提供：

- `sendJson()`
- `beginJson()`
- `writeJsonEscaped()`
- `endJson()`

所有内置 JSON API 必须对字符串字段做 JSON escape。

### 7.5 LittleFS 策略

LittleFS 默认不自动格式化：

```cpp
ESP32BASE_FS_AUTO_FORMAT=0
```

只有显式开启才格式化。

### 7.6 Config 策略

Config 必须修正：

- 读取用 readonly NVS。
- deferred write 支持 delay。
- `handle()` 每轮最多写一条。
- `flushAll()` 写完所有 pending。
- restart / deep sleep 前必须 `flushAll()`。

### 7.7 OTA 策略

OTA 必须修正：

- 未授权请求不能进入写 flash 流程。
- 上传期间喂狗或临时暂停 watchdog。
- 记录 progress、bytes、error。
- OTA 成功后通过统一 restart 流程重启。

## 8. 示例项目

示例按 profile 组织：

- `examples/core_basic`
  - `ESP32BASE_PROFILE_CORE`
- `examples/runtime_basic`
  - `ESP32BASE_PROFILE_RUNTIME`
- `examples/wifi_basic`
  - `ESP32BASE_PROFILE_NET`
- `examples/web_config`
  - `ESP32BASE_PROFILE_WEB`
- `examples/web_ota`
  - `ESP32BASE_PROFILE_FULL`
- `examples/full_diagnostics`
  - `ESP32BASE_PROFILE_FULL`

每个示例都包含：

- ESP32
- ESP32-S3
- ESP32-C3

## 9. 测试计划

### 9.1 编译验证

- 每个 profile 至少编译 ESP32 / ESP32-S3 / ESP32-C3。
- `CORE` 不应链接 WiFi、WebServer、Update、LittleFS。
- `NET` 不应链接 WebServer、Update。
- `WEB` 不应链接 Update。
- `FULL` 全部链接并通过容量检查。

### 9.2 实机验证

- CORE：无 AP、无 Web、无 FS 挂载。
- RUNTIME：Watchdog、Sleep、LittleFS 文件读写。
- NET：无凭证进 AP 配网，有凭证 STA 非阻塞连接，NTP / mDNS 延迟启动。
- WEB：Basic Auth、状态 API、WiFi 配置 API、自定义路由。
- FULL：OTA 成功、未授权 OTA 被拒绝、OTA 期间 watchdog 安全。
- Config：deferred 多条写入在 restart / deep sleep 前全部落盘。
- JSON：SSID / hostname 含引号、反斜杠、控制字符时仍输出合法 JSON。

### 9.3 资源验证

记录每个 profile：

- 静态 RAM 占用
- Flash 占用
- 启动后 free heap
- WiFi connected free heap
- Web request min heap
- OTA min heap
- LittleFS 操作 min heap

## 10. 文档调整

文档全部按新架构重写：

- 设计说明
- 架构说明
- Profile 使用说明
- API 说明
- Web 扩展说明
- 内存预算
- 诊断与实机测试

README 第一屏必须明确说明：

- 默认 profile 是 CORE。
- 如何选择 profile。
- 不同 profile 会影响链接体积、启动行为和运行资源。

## 11. 明确不做

- 不考虑历史兼容性。
- 不支持 ESP8266。
- 不保留跨芯片 HAL。
- 不加入 MQTT。
- 不加入 HTTP Client。
- 不加入 WebSocket。
- 不加入复杂 Scheduler。
- 不加入文件日志。
- 不把本库做成 IoT 云框架或大型管理平台。

## 12. 假设

- ESP32 / ESP32-S3 / ESP32-C3 都是一等目标。
- 资源可控、模块顺序合理、长期可维护优先于最少改动。
- 当前实现可以全部替换。
- 旧 `EspBase` 只作为设计经验参考，不作为 API 兼容目标。

---

# 方案评估提示词

```text
请以资深嵌入式系统架构师、ESP32 Arduino Core 库维护者、长期量产设备固件负责人三重视角，严谨评估这份 Esp32Base 重构方案。

评估重点包括：

1. 架构合理性：
   - Core / Runtime / Network / Web / Update 五层划分是否清晰、必要、不过度设计？
   - 模块依赖顺序是否正确？是否存在隐藏的反向依赖或循环依赖？
   - Esp32Base 作为统一入口是否会重新变成过重的中心化控制器？

2. 资源裁剪能力：
   - profile 设计是否真正能让只使用部分功能的应用节省 Flash、RAM 和启动资源？
   - 编译期宏裁剪是否足够明确？
   - CORE / RUNTIME / NET / WEB / FULL 这些场景是否覆盖真实项目主要需求？是否太多或太少？

3. ESP32 专用优化：
   - 不再支持 ESP8266、不保留跨芯片 HAL 是否合理？
   - 对 ESP32 / ESP32-S3 / ESP32-C3 是否有明确收益？
   - 是否遗漏 ESP32 Arduino Core 常见限制、坑点或版本兼容问题？

4. API 与长期维护：
   - 公开模块命名和职责是否稳定？
   - 拆分 Runtime、Fs、Watchdog、Sleep、Ota 是否比当前 5 类设计更合理？
   - 是否会造成用户使用复杂度过高？
   - 文档和示例结构是否足以支撑这种架构？

5. 可靠性与安全：
   - Config deferred write、flushAll、restart/deep sleep 前落盘策略是否完整？
   - Web Basic Auth、OTA 认证边界、OTA 上传期间 watchdog 处理是否严谨？
   - JSON escape、LittleFS 自动格式化策略、NVS readonly 读取是否处理到位？
   - 是否还有未覆盖的错误恢复、状态机、资源泄漏或实机风险？

6. 实施风险：
   - 如果允许删除重写，推荐采用一次性大重构还是分阶段迁移？
   - 哪些模块应优先实现？
   - 哪些设计点必须先做原型验证？
   - 哪些部分可能看似合理但实际会增加维护成本？

请输出：
- 总体结论：是否推荐采用
- 主要优点
- 主要风险
- 必须修改的设计点
- 可选优化建议
- 推荐的最终模块顺序
- 推荐的实施阶段
- 需要实机验证的关键场景
- 如果你认为有更合理的替代架构，请给出完整替代方案
```
