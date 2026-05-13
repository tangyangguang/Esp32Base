# Esp32Base

`Esp32Base` 是面向 ESP32 / ESP32-S3 / ESP32-C3、基于 Arduino ESP32 Core 的轻量基础库设计。

本仓库当前按 `docs/` 中的新架构实施。历史设计方案与评审记录已归档到 `design-history/`，不参与编译。

## 定位

`Esp32Base` 不是 Web 管理平台、云框架或大型组件系统，而是一套可裁剪的 ESP32 应用基础底座。

设计目标：

- 极简应用只付 Core 成本。
- 常见量产设备通过 profile 选择能力组合。
- 未启用模块不编译、不链接、不初始化。
- WiFi、Web、OTA 等重能力全部非阻塞启动。
- OTA、NVS、Watchdog、LittleFS、Captive Portal 等关键路径按量产可靠性设计。

已知限制、明确不支持能力和风险边界详见 [已知限制](docs/10_known_limitations.md)。

## 快速开始

最小应用：

```cpp
#include <Esp32Base.h>

void setup() {
    Esp32Base::setFirmwareInfo("demo", "1.0.0");
    Esp32Base::begin();
}

void loop() {
    Esp32Base::handle();
}
```

启用能力通过 profile 或精细宏完成：

```cpp
#define ESP32BASE_PROFILE ESP32BASE_PROFILE_WEB_RUNTIME
#include <Esp32Base.h>
```

默认 hostname 通过构建配置指定，不在应用代码中调用 setter：

```ini
build_flags =
  -D ESP32BASE_DEFAULT_HOSTNAME=\"esp32-demo\"
```

Web/API 保存的 hostname 存储在 `eb_sys.hostname`，重启后覆盖构建默认值；出厂重置清除该配置后恢复 `ESP32BASE_DEFAULT_HOSTNAME`。

启用 FS 的 profile 会默认启用 Runtime 文件日志：`/logs/eb_app.log`，默认 `4 × 32KB`，模式 WARN。示例通过构建参数把默认模式改为 INFO：

```ini
build_flags =
  -D ESP32BASE_EB_FILELOG_DEFAULT_MODE=ESP32BASE_FILELOG_MODE_INFO
```

## 支持目标

芯片：

- ESP32
- ESP32-S3
- ESP32-C3

框架：

- Arduino ESP32 Core 2.0.14+
- Arduino ESP32 Core 3.0.4+

不支持：

- ESP8266
- 跨芯片 HAL
- MQTT / HTTP Client 封装
- WebSocket
- HTTPS
- 多用户权限
- 大型前端 SPA
- 云平台绑定

## Profiles

默认 profile 是 `ESP32BASE_PROFILE_CORE`。

| Profile | Core | Runtime | Bus | Network | Web | OTA | 用途 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `ESP32BASE_PROFILE_CORE` | yes | no | no | no | no | no | 极简配置和诊断 |
| `ESP32BASE_PROFILE_RUNTIME` | yes | yes | yes | no | no | no | 离线本地设备 |
| `ESP32BASE_PROFILE_NET` | yes | no | no | yes | no | no | 纯联网设备 |
| `ESP32BASE_PROFILE_NET_RUNTIME` | yes | yes | yes | yes | no | no | 量产联网节点 |
| `ESP32BASE_PROFILE_WEB` | yes | no | yes | yes | yes | no | Web 配置和状态 |
| `ESP32BASE_PROFILE_WEB_RUNTIME` | yes | yes | yes | yes | yes | no | Web + 本地可靠运行 |
| `ESP32BASE_PROFILE_FULL` | yes | yes | yes | yes | yes | yes | 完整管理和 OTA |

Profile 是默认组合，用户仍可用 `ESP32BASE_ENABLE_*` 精细覆盖。所有覆盖都必须经过编译期依赖检查。

文件日志是 Runtime/FS 能力，不属于 Core。`CORE` 和默认 `NET` 不链接 LittleFS；`RUNTIME`、`NET_RUNTIME`、`WEB_RUNTIME`、`FULL` 默认可用文件日志。

仓库示例默认面向 ESP32 4MB Flash，并使用 `partitions/esp32-4mb-ota-balanced.csv`。ESP32-S3、ESP32-C3 或 8MB 板型请优先使用示例中对应 env，或在业务项目里选择匹配芯片和 Flash 容量的分区表，避免 FULL/Web OTA 固件超过 app slot。

`Esp32BaseFs` 对业务暴露文本、二进制、追加、目录和容量 API，并提供 `readBytesAt()` / `writeBytesAt()` 按偏移读写能力。业务可通过这些 API 实现二进制定长日志分页读取和环形覆盖写入，不需要 include `LittleFS.h` 或 Arduino `File`。

`ESP32BASE_PROFILE_FULL` 默认同时支持 Web OTA 和 PlatformIO/espota 命令行 OTA。命令行 OTA 使用 `Esp32Base::hostname()` 对应的 `<hostname>.local`、标准端口 3232，以及当前 Web Auth 密码：

```ini
upload_protocol = espota
upload_port = esp32-demo.local
upload_flags =
  --auth=admin
```

不需要命令行 OTA 的业务可显式设置 `ESP32BASE_ENABLE_ARDUINO_OTA=0`，现有 Web OTA 不受影响。

需要更快的命令行上传时，可使用 Esp32Base 内置 `webota` target。它复用现有 HTTP Web OTA 接口和 Web Auth，推荐直接配置设备 IP：

```ini
extra_scripts =
  post:path/to/Esp32Base/scripts/esp32base_webota.py

custom_esp32base_webota_host = 192.168.2.112
custom_esp32base_webota_user = admin
custom_esp32base_webota_password = admin
```

```sh
pio run -t webota
```

`espota` 是 ArduinoOTA 标准协议；`webota` 是 Esp32Base 提供的 HTTP 上传方式，默认较大分块并在上传前预检认证，通常更适合反复快速烧录，但要求设备 Web OTA 可访问。

## 文档入口

建议按顺序阅读：

0. [能力变更记录](CHANGELOG.md)
1. [设计说明](docs/00_design.md)
2. [架构说明](docs/01_architecture.md)
3. [Profile 与裁剪](docs/02_profiles.md)
4. [API 契约](docs/03_api.md)
5. [Web 与配网](docs/04_web.md)
6. [OTA 与回滚](docs/05_ota.md)
7. [内存与容量预算](docs/06_memory_budget.md)
8. [诊断与测试](docs/07_diagnostics.md)
9. [Arduino Core 兼容性](docs/08_arduino_core_compat.md)
10. [发布检查清单](docs/09_release_checklist.md)
11. [已知限制](docs/10_known_limitations.md)

历史设计方案与评审记录已归档到 `design-history/`，只作为背景材料，不作为新实现的直接依据。

## 最终实施原则

- 文档和代码必须保持一致。
- 首版发布前按最终最优实现推进，不保留旧 key、旧 API 或旧行为迁移包袱。
- Profile 裁剪必须以编译产物、map 文件和符号检查验证。
- 发布后不再做大规模架构重构，只做 bug 修复、兼容性维护、文档修正和必要小范围适配。
