# Esp32Base 设计说明

## 1. 项目定位

`Esp32Base` 是面向 ESP32 系列、基于 Arduino Core 的应用基础库。

目标芯片：

- ESP32
- ESP32-S3
- ESP32-C3

这是一个全新项目。原 `EspBase` 只作为经验参考，新项目不保持旧 API 兼容，不提供迁移层，也不再包含 ESP8266 分支。

`Esp32Base` 的定位是：

```text
ESP32 应用基础运行底座
```

不是：

```text
Web 管理平台
物联网云框架
大型组件系统
跨平台 HAL
```

## 2. 已确认的设计决策

- 应用自定义 Web 页面和 API 可以在 `Esp32Base::begin()` 前或后注册。
- 如果没有保存 WiFi 凭证，Network 模块应自动进入 AP 配置模式。
- Web Basic Auth 默认开启。开发默认账号密码为 `admin` / `admin123`，并允许用宏覆盖。
- 库内部 NVS 数据使用 `esp32base` namespace。应用可以使用自己的短 namespace，但不能使用 `esp32base`。
- Phase 1 只实现入口、内部日志、NVS 配置、Runtime 系统信息、启动诊断和 PlatformIO basic 样例。

## 3. 功能边界

第一版最终必须包含：

- 内部日志
- 系统信息与资源诊断
- NVS 配置
- WiFi STA 自动连接
- 无凭证时 AP 配置模式
- Web 基础服务
- Web OTA
- NTP 对时
- mDNS
- LittleFS 基础文件读写
- Watchdog
- Deep sleep / restart
- 应用自定义 Web 页面/API

第一版不做：

- MQTT
- HTTP Client 业务封装
- 蓝牙
- ESP-NOW
- 文件日志
- 通用任务队列
- 复杂 Scheduler
- 完整文件管理器
- WebSocket
- HTTPS
- 多用户权限
- 大型前端页面、图表、主题或 SPA
- 云平台绑定
- HAL 抽象

## 4. 公开模块

公开 API 收敛为 5 个类：

```text
Esp32Base
├── Esp32BaseConfig
├── Esp32BaseNetwork
├── Esp32BaseWeb
└── Esp32BaseRuntime
```

`Esp32Base` 是唯一推荐初始化入口，负责模块初始化顺序、固件信息、hostname、启动诊断、`begin()` 和 `handle()`。

`Esp32BaseConfig` 负责 NVS key/value 配置、写前比较、延迟写入，以及 restart / deep sleep 前 flush。

`Esp32BaseNetwork` 负责 WiFi、NTP 和 mDNS。这三个能力不拆成独立公开类，避免状态和 API 分散。

`Esp32BaseWeb` 负责 WebServer、Basic Auth、WiFi 设置页、OTA、状态 API，以及应用自定义页面/API 注册。OTA 属于 Web 模块，认证边界只有一处。

`Esp32BaseRuntime` 负责 reset reason、wake reason、资源快照、restart、deep sleep、Watchdog 和最小 LittleFS 文件能力。

## 5. 命名规范

公开类：

```text
Esp32Base
Esp32BaseConfig
Esp32BaseNetwork
Esp32BaseWeb
Esp32BaseRuntime
```

宏：

```text
ESP32BASE_LOG_LEVEL
ESP32BASE_WEB_MAX_APP_ROUTES
ESP32BASE_CONFIG_PENDING_MAX
ESP32BASE_FILE_READ_MAX
ESP32BASE_HOSTNAME_LEN
ESP32BASE_WEB_AUTH_USER
ESP32BASE_WEB_AUTH_PASS
```

日志 tag：

```text
Esp32Base
BaseCfg
BaseNet
BaseWeb
BaseRun
```

保留 NVS namespace：

```text
esp32base
```

应用业务不要使用 `esp32base` namespace。

## 6. 目录规范

正式文档统一放在 `docs/` 目录下，并且所有文档文件名前面都加编号。

所有样例程序都使用 PlatformIO 项目格式，至少包含：

```text
platformio.ini
src/main.cpp
```

推荐结构：

```text
Esp32Base/
├── library.json
├── README.md
├── docs/
│   ├── 00_design.md
│   ├── 01_architecture.md
│   ├── 02_api.md
│   ├── 03_web_extension.md
│   ├── 04_memory_budget.md
│   └── 05_diagnostics.md
├── examples/
│   ├── basic/
│   ├── wifi_network/
│   ├── wifi_web_ota/
│   ├── custom_web/
│   └── sleep_watchdog/
└── src/
    ├── Esp32Base.h
    ├── Esp32Base.cpp
    ├── Esp32BaseConfig.h
    ├── Esp32BaseConfig.cpp
    ├── Esp32BaseNetwork.h
    ├── Esp32BaseNetwork.cpp
    ├── Esp32BaseWeb.h
    ├── Esp32BaseWeb.cpp
    ├── Esp32BaseRuntime.h
    └── Esp32BaseRuntime.cpp
```

## 7. 初始化流程

`Esp32Base::begin()`：

```text
Serial log
System info snapshot
Config/NVS
Runtime begin
Network begin
Web begin
Ready
```

`begin()` 不等待 WiFi 连接，不阻塞 NTP 同步。

`Esp32Base::handle()`：

```text
Config flush one pending write
Network handle
Web handleClient
Runtime handle
One-shot startup diagnostics
```

## 8. 分阶段计划

Phase 1：

- `Esp32Base`
- 内部日志宏
- `Esp32BaseConfig`
- `Esp32BaseRuntime` system info / restart / heap
- 启动诊断
- PlatformIO `examples/basic`
- 编译 ESP32 / ESP32-S3 / ESP32-C3

Phase 2：

- WiFi STA
- 保存 WiFi 凭证
- 无凭证时 AP 配置模式
- NTP
- mDNS

Phase 3：

- WebServer
- Basic Auth
- WiFi 设置页
- OTA 页面/API
- 状态 API
- 应用在 `begin()` 前后都可注册页面/API

Phase 4：

- Watchdog
- Deep sleep
- LittleFS 基础文件能力
- PlatformIO `examples/runtime_tools`

Phase 5：

- 实机内存与可靠性压测

## 9. 资源目标

ESP32 DevKit、无 PSRAM 的目标：

| 场景 | 目标 free heap |
| --- | --- |
| 仅启动核心模块 | >= 250KB |
| WiFi 已连接，Web 已启动 | >= 210KB |
| Web 页面/API 活跃 | >= 180KB |
| OTA 上传中最低值 | >= 120KB |
| LittleFS 小文件操作中 | >= 170KB |
| 全功能示例启动后 | >= 170KB |

硬约束：

- 第一版不依赖 PSRAM。
- 栈上临时缓冲默认不超过 1KB。
- Web 响应使用分段输出，避免大 `String` 拼接。
- 路由表和队列使用固定容量。
- 容量固定或通过宏配置。

## 10. 可靠性规则

- WiFi 重连不阻塞主循环。
- NTP 同步不阻塞主循环。
- OTA 必须经过 Web Auth。
- OTA 期间必须喂狗或暂停 Watchdog。
- Restart 和 deep sleep 前必须 flush config。
- NVS 写入前比较旧值，未变化不 commit。
- 延迟配置写入每次 `handle()` 最多提交一条。
