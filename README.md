# Esp32Base

`Esp32Base` 是面向 ESP32 / ESP32-S3 / ESP32-C3、基于 Arduino ESP32 Core 的轻量基础库设计。

本仓库当前按 `docs/` 中的新架构实施。历史设计方案、评审记录和临时评估文件已从发布分支清理，不参与编译，也不作为实现依据。

## 定位

`Esp32Base` 不是 Web 管理平台、云框架或大型组件系统，而是一套可裁剪的 ESP32 应用基础底座。

设计目标：

- 极简应用只付 Core 成本。
- 常见量产设备通过 profile 选择能力组合。
- 未启用模块不编译、不链接、不初始化。
- WiFi、Web、OTA 等重能力全部非阻塞启动。
- WiFi STA 启动带安全保护：连续 STA guarded brownout/panic/watchdog 复位后暂停已保存凭据并回退 AP 配网；后续正常上电会自动恢复一次，Web 也可直接重试已保存凭据，避免坏 STA 状态造成永久重启循环。
- WiFi 初始化阶段连续复位时会先跳过所有 Arduino WiFi 初始化调用，让系统进入无 WiFi 诊断状态，并用中文日志提示疑似 WiFi/RF 启动瞬时电流导致供电跌落，建议检查电源、USB 线、稳压器、接线并考虑在板端 VIN/5V 与 GND 间增加低 ESR 储能电容。
- OTA、NVS、Watchdog、LittleFS、Captive Portal 等关键路径按量产可靠性设计。

已知限制、明确不支持能力和风险边界详见 [已知限制](docs/10_known_limitations.md)。

Web 页面结构、样式基线、业务页面模板和换肤策略详见 [Web UI 页面结构与样式基线](docs/11_web_ui_baseline.md)。
业务项目接入前，建议先用 `examples/web_ui_gallery` 统一查看和验证状态、记录、配置、命令、流程、确认和空状态等页面样式；`examples/full_demo` 侧重完整功能集成。
基础 Web CSS 由 `/esp32base/ui.css` 统一输出并允许浏览器缓存，业务页面通过 `sendHeader()` 自动引用，不需要复制样式；按钮默认按轻量设备控制台风格收敛，普通入口和分页使用浅底描边，明确保存/执行才使用主按钮；原生 `<dialog class="panel eb-modal">` 也会获得统一的浅色弹层、遮罩、表单和按钮对齐。

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

Web/API 保存的 hostname 存储在 `eb_sys.hostname`，重启后覆盖构建默认值，并用于 DHCP client hostname、mDNS 和 OTA；出厂重置清除该配置后恢复 `ESP32BASE_DEFAULT_HOSTNAME`。

底部横条可在 System 页面配置为 Off、Status only 或 Links + status。该设置保存到 `eb_ui.footer_mode`，用于控制 `sendFooter()` 输出的紧凑系统入口和运行摘要。

Web 应用路由默认容量统一为 24。该上限只覆盖业务 `addRoute()` / `addPage()` / `addApi()` 注册的静态路由表；内置 Web 路由不占用此表。route 较少且需要节省静态 RAM 的应用，可在完成 route 数量核算后通过构建参数显式调小 `ESP32BASE_WEB_MAX_ROUTES`。

业务首页推荐注册为 `/index`。在 `HOME_APP` 或 `HOME_COMBINED` 下，如果没有显式 `setHomePath()` 且存在 `/index` 业务页，裸 `/` 会跳转到 `/index`；如果应用确实需要直接渲染裸根路径，可显式 `setHomePath("/")` 并注册 GET `/` 业务页。`/esp32base` 始终保留为基础库系统入口。

启用 FS 的 profile 会默认启用 Runtime 文件日志：`/logs/eb_app.log`，默认 `4 × 32KB`，模式 WARN。运行时可配置为 OFF、ERROR、WARN、INFO；如果 FS 满或文件损坏导致写入失败，Web 会显示 FileLog 运行态为 `write fault`，表示配置仍开启、已有日志可能仍可读取，但新日志写入已被保护停写。`disabled` 表示模式为 OFF，新日志不会写入。示例通过构建参数把默认模式改为 INFO：

```ini
build_flags =
  -D ESP32BASE_EB_FILELOG_DEFAULT_MODE=ESP32BASE_FILELOG_MODE_INFO
```

需要记录业务可解释事件时，可显式启用通用应用事件日志。它默认关闭，不随 FULL profile 自动开启；启用后依赖 FS，默认在 `/app/events.bin` 保存 `1024` 条固定结构记录，约 `188 KiB`。App Events 是面向长期运行设备的“近期关键事件窗口”，用于解释最近一段业务行为；它不是业务长期数据模型，不替代统计、报表、累计量、传感器采样历史、完整执行历史或不允许覆盖的业务数据。应用项目负责决定事件何时写入、`source/type/reason/object` 如何命名以及页面文案如何解释；基础库只提供结构化写入、固定容量环形覆盖、分页读取、Web/API/CSV 展示和按需清空。

```ini
build_flags =
  -D ESP32BASE_PROFILE=ESP32BASE_PROFILE_FULL
  -D ESP32BASE_ENABLE_APP_EVENTS=1
```

内置入口为 `/esp32base/app-events`，JSON API 为 `/esp32base/api/app-events?offset=0&limit=50`，CSV 导出为 `/esp32base/app-events.csv`，清空入口位于 System 页的危险操作区。内置事件日志页面用偏底层视角展示事件日志文件、slot、状态、CRC 和完整记录字段；JSON API 只输出有效事件，适合业务系统创建自己的业务事件列表和详情页并用业务语言解释。页面/API/CSV 支持等级、时间类型、来源、类型、原因和关键词筛选；时间优先显示可信真实时间，无法解析时显示 `uptime N ms` 和 boot id。单条损坏记录不会让整个日志停写；结构性 header/写入故障才进入 fault。该能力明确独立于 `/esp32base/logs` 的系统 FileLog，不混入 WiFi、OTA、NTP、启动、健康状态等基础库系统日志。样例见 `examples/app_events_demo`。

App Events 适合记录计划跳过、保护触发、运行异常、外部 API 决策、用户清除告警等低频关键事件。不适合记录用水量长期统计、报表数据、累计值、传感器采样序列、大 payload、高频明细或必须永久保留的完整业务历史；这些应继续由业务自己的数据模型和文件负责。

判定规则：FileLog/Status/System diagnostics 记录系统底层发生了什么，App Events 记录业务为什么做了某个决定。应用项目不要把 Esp32Base 系统事件写入 App Events，例如 boot/reset/restart reason、WiFi、NTP、OTA、LittleFS mount/write fault、FileLog fault、基础库健康状态等。同一个故障可以同时有 FileLog 和 App Event，但不能机械重复：FileLog 写技术原因和内部链路，App Event 写业务含义和用户可理解结果。硬件或存储异常只有在导致业务保护、跳过、停机、业务告警或用户维护动作时，才以业务事件写入 App Events。

需要业务持久化参数配置页时，可启用 App Config。业务显式声明容量并在 `Esp32Base::begin()` 前注册分组和字段，基础库会在 System 页首位提供 `App Config` 入口：

```ini
build_flags =
  -D ESP32BASE_ENABLE_APP_CONFIG=1
  -D ESP32BASE_APP_CONFIG_MAX_GROUPS=3
  -D ESP32BASE_APP_CONFIG_MAX_FIELDS=8
```

App Config 支持 string、int、decimal 定点数、bool 和 enum 字段；保存时后端重新校验并只写入实际变化字段。注册传入的字符串和 enum option 数组需保持固件生命周期有效。标记为重启后生效的字段保存后会在未重启会话内持续提示旧值和已保存新值，适合低频修改的小型业务配置。

Web 页面应优先使用 Esp32Base 的 UI baseline、helper 和页面能力块；不要在业务项目里为样式问题临时复制 CSS 或绕开基础库。单字段轻量编辑可使用行内编辑 helper，小型 1-3 字段表单可使用弹层表单 helper；二者会在支持 `fetch` 的浏览器中局部提交和局部替换，禁用 JS 时仍回退到普通表单提交。需要业务自控打开/关闭时，也可直接使用原生 `<dialog class="panel eb-modal">` 并复用 `fieldgrid`、`actions`、`btnlink`。找不到合适页面能力块时，先查看 [Web UI 页面结构与样式基线](docs/11_web_ui_baseline.md)，并优先回到 Esp32Base 评估是否补充统一能力。

联网 profile 可通过 `Esp32BaseNtp::snapshot()` 获取统一业务时间快照。断网启动时业务仍能记录当前 `bootId + uptimeSec`；`bootId` 复用系统 boot count，不为 NTP 额外写启动期 NVS。本次 boot 后续 NTP 同步成功时，`onTimeSynced()` 会回调，业务可用 `resolveCurrentBootEvent()` 只回填同一 boot 的相对时间事件，历史未知时间不会被伪造为日期。

WiFi 默认关闭 modem sleep，让 Web 首屏和 OTA 不被 Arduino ESP32 默认 `WIFI_PS_MIN_MODEM` 的 DTIM 唤醒抖动拖慢；电池设备可调用 `Esp32BaseWiFi::setPowerSave(true)` 恢复 modem sleep。

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

`Esp32BaseFs` 对业务暴露文本、二进制、追加、定长文件、目录和容量 API，并提供 `readBytesAt()` / `writeBytesAt()` 按偏移读写能力。业务可通过这些 API 实现二进制定长日志分页读取和环形覆盖写入，不需要 include `LittleFS.h` 或 Arduino `File`。固定容量环形文件应先用 `createFixedFile()` 确保容量，再用 `writeBytesAt()` 覆盖槽位；`appendBytes()` 适合低频追加，会做写后大小和末端可读校验，不适合作为大文件 bulk 初始化循环。业务必须检查这些 API 的返回值；当 LittleFS 元数据仍声明文件大小但内容块不可读、FS 满或写后校验失败时，API 会返回 `false`。内置 `/esp32base/fs` 会标记这类文件为 `unreadable`，只在管理模式提供删除，不提供下载。`/esp32base/fs?manage=1` 还提供受限上传，用于测试期导入业务数据文件：上传保留本地文件名，可选择任何已有目录，不创建目录；同名文件会在浏览器确认后覆盖。

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

双 OTA 设备如果已经通过 Web OTA 切换到另一个槽位，普通串口 `pio run -t upload` 通常只写 `board_upload.offset_address` 对应的固定 app 槽，不会自动覆盖当前运行槽，也不会修改 `otadata`；设备仍可能继续从旧的 `ota_1` 启动。串口恢复优先使用基础库脚本，它会按分区表写入两个 OTA app 槽，并在同一次 `write_flash` 里写入全 `0xFF` 的 `otadata` 镜像完成清理；如果 `boot_app0` 默认偏移与 `otadata` 重叠，脚本会跳过 `boot_app0`，以清理 `otadata` 为准：

```sh
python path/to/Esp32Base/scripts/esp32base_serial_recover_ota.py \
  -d path/to/business/project \
  -e esp32dev \
  --port /dev/ttyUSB0
```

可先加 `--dry-run` 查看将执行的 `esptool.py` 命令和 `otadata` 处理方式；特殊分区表或板型可显式覆盖 bootloader、partition table 和 boot_app0 偏移。执行恢复前先关闭 `pio device monitor`、串口调试器和其他占用同一端口的程序；如果 CH340/CP210x 转串口在高波特率下出现 `Serial data stream stopped` 或连接中断，可用 `--baud 115200` 降速重试。

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
12. [Web UI 页面结构与样式基线](docs/11_web_ui_baseline.md)

历史设计方案、评审记录和临时评估文件已从发布分支清理，不作为新实现的直接依据。

## 最终实施原则

- 文档和代码必须保持一致。
- 首版发布前按最终最优实现推进，不保留旧 key、旧 API 或旧行为迁移包袱。
- Profile 裁剪必须以编译产物、map 文件和符号检查验证。
- 发布后不再做大规模架构重构，只做 bug 修复、兼容性维护、文档修正和必要小范围适配。
