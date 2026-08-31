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
- `Esp32BaseRecordStore`
- `Esp32BaseAppEvents`
- `Esp32BaseTime`
- `Esp32BaseRtc`
- `Esp32BaseRs485Port`
- `Esp32BaseHealth`

职责：

- loop-task-only 同步事件总线。
- Task Watchdog。
- Sleep。
- LittleFS。
- 系统诊断日志 sink（`Esp32BaseFileLog`），依赖 Fs，通过 Core Log 的 line sink 接收日志。
- 固定长度业务 Record Store，依赖 Fs 和 Time，提供按段追加、完整段淘汰、CRC恢复和分页读取，不解释payload业务语义。
- App Events 复用 Record Store，是基础库预定义紧凑payload的一类低频业务事件记录，不是第二套系统诊断日志。可选条件跟踪位于同一Runtime模块，只接受应用观察结果并用Core NVS保存固定32位活动ID集合；它不依赖或解释RTC、传感器、门锁等业务模块。
- 统一可信时间门面（`Esp32BaseTime`），整合 uptime、RTC 和 NTP。
- 外部 RTC 时间源（`Esp32BaseRtc`），支持 DS3231 / PCF8563 构建期二选一。
- RS485 半双工串口方向控制（`Esp32BaseRs485Port`），封装 `HardwareSerial`、RX/TX/DE 引脚、发送前后 DE 切换和轮询读取，不包含 Modbus 或业务协议。
- 健康诊断。

Runtime 只依赖 Core。

`Esp32BaseLog` 仍属于 Core，只负责 Serial、格式化和 sink 分发，不包含 LittleFS。系统诊断日志由 Runtime 的 `Esp32BaseFileLog` 承担，避免 Core 依赖 FS。

## 4. Layer 2: Network

模块：

- `Esp32BaseWiFi`
- `Esp32BaseDns`
- `Esp32BaseNtp`
- `Esp32BaseMdns`
- `Esp32BaseMqtt`（独立能力宏，默认关闭）

职责：

- WiFi STA / AP 状态机。
- Captive Portal DNS。
- NTP。
- mDNS。
- 单 Broker MQTT 3.1.1 TCP/TLS Client、固定邮箱、退避重连和协议诊断。

Network 可使用 Bus，但不强依赖 Bus。

MQTT 只向下依赖 WiFi、统一 Time 和 Core 日志/诊断，不直接调用 NTP 模块 API；TLS 契约要求统一 Time 的来源为 NTP。ESP-MQTT 的 FreeRTOS task 不直接调用应用 callback；底层事件先复制到固定容量消息槽或控制事件环，再由 `Esp32Base::handle()` 所在的 loop/system task 分发。OTA、restart 和 deep sleep 由顶层 `Esp32Base` facade 通知 MQTT 暂停或异步断开，Network 不反向依赖 Update、Sleep 或 Web。

公网 TLS 只在统一 Time 来源升级为 NTP 后启动；RTC 仍可服务离线业务时间，但不单独放行 MQTTS。官方预编译 Arduino Core 2.0.16/3.3.8 的 mbedTLS 未启用 `CONFIG_MBEDTLS_HAVE_TIME_DATE`，因此 wrapper 默认拒绝这种 MQTTS 配置；应用只有显式接受“CA 链和 hostname 校验仍在、证书日期不校验”的限制后才能构建 opt-in。这里不增加第二次阻塞 TLS 预握手，也不依赖 ESP-MQTT/ESP-TLS 私有句柄去补做日期校验。

底层选择 ESP-MQTT，不额外引入 Arduino MQTT 依赖。选择依据是 Core 2.x/3.x 均随包提供、三类目标芯片共用 ESP-IDF transport、具备 QoS 1/LWT/retain/分片事件和异步 task；wrapper 负责收窄为 MQTT 3.1.1、固定容量、loop callback 和安全默认值。基于 `WiFiClientSecure` 的 Arduino Client 路径虽然 API 更小，但需要新增并维护第三方依赖，且常见实现的 loop/connect 阻塞、QoS 1 ACK、分片和缓冲上限契约不一致。完全由各业务项目自行接入的路径不增加基础库体积，却会让 TLS gate、退避抖动、生命周期和诊断在多个项目重复实现。ESP-MQTT功能较多造成的Flash/task/heap代价由IOT Profile、独立能力宏和实测预算显式承担；LOCAL及更小Profile不为MQTT付成本。

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

Web 是多编译单元实现，不再通过单个巨大 `.inc` 承载全部页面。`library.json` 必须显式编译 `src/web/*.cpp` 和 `src/web/internal/*.cpp`；每个Web源文件仍用能力宏保护，确保MINIMAL/OFFLINE及显式关闭Web的特殊组合不链接WebServer、Update等重依赖。

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

Core 不 include Runtime/FileLog、Web、Network 或 OTA。重启和休眠前需要落盘的 Runtime 资源通过 `Esp32BaseSystem` 的轻量生命周期 hook 由 `Esp32Base` facade 编排，Core 只负责记录 restart reason、flush Config 和执行底层 restart。

## 8. begin 顺序

`Esp32Base::begin()` 当前顺序约束：

1. Log
2. Config
3. System
4. OTA boot 初始化，如启用 OTA；该步骤只处理 rollback/mark-valid 相关启动状态，不启动网络 OTA 服务
5. Time boot session，如启用
6. RTC，如启用
7. NTP boot session / AppEvents time provider，如启用
8. Bus，如启用
9. Fs，如启用
10. AppEvents，如启用
11. FileLog，如启用
12. Watchdog，如启用
13. Sleep，如启用
14. Health，如启用
15. WiFi，如启用
16. 标记 ready

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

`Esp32Base::handle()` 当前顺序约束：

1. 如果 OTA 正在上传，跳过 Config deferred flush。
2. 否则 Config deferred flush，一轮最多一条。
3. FileLog handle，如启用。
4. RTC handle，如启用。
5. WiFi handle。
6. Captive DNS handle。
7. 条件启动 Web。
8. 条件启动 NTP。
9. MQTT 状态机、固定邮箱和业务 callback；OTA 上传期间暂停。
10. 条件启动 mDNS。
11. Web handle。
12. OTA handle，包含 mark-valid timeout检查。
13. Health handle。
14. Watchdog feed。
15. 启动阶段诊断日志。

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
4. MQTT 尽力异步请求 DISCONNECT，不等待或停止后台 task；task 随本次 restart / deep sleep 终止，不承诺 DISCONNECT、在途 publish 或 PUBACK 已抵达。
5. 输出最后诊断。
6. 处理 Watchdog。
7. 执行底层 restart / sleep。

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
├── Esp32BaseCapabilityConfig.h
├── core/
├── runtime/
├── network/
├── web/
└── update/
```

不提供全局短别名头文件，避免 `Log`、`Config`、`Web` 等名字污染应用全局命名空间。

## 13. Profile 裁剪实现模型

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
