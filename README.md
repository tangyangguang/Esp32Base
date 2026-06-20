# Esp32Base

`Esp32Base` 是面向 ESP32 / ESP32-S3 / ESP32-C3、基于 Arduino ESP32 Core 的轻量基础库设计。

本仓库当前按 `docs/` 中的新架构实施。历史设计方案、评审记录和临时评估文件已从发布分支清理，不参与编译，也不作为实现依据；发布包排除 docs/superpowers 这类过程文档目录。

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

Web 应用路由默认容量统一为 24。该上限只覆盖业务 `addRoute()` / `addPage()` / `addApi()` 注册的静态路由表；内置 Web 路由不占用此表。route 较少且需要节省静态 RAM 的应用，可在完成 route 数量核算后通过构建参数显式调小 `ESP32BASE_WEB_MAX_ROUTES`。业务 CSS/JS/图片等固定内容应优先用 `Esp32BaseWeb::addStaticAsset()` 注册到基础库 WebServer；该能力不占用应用 route 表，默认带 Basic Auth、固定 `Content-Length`、`nosniff` 和可配置浏览器缓存，避免业务项目绕开 WebServer 或复制静态资源发送逻辑。

应用项目需要在 native 单元测试中直接执行 Web handler 时，可启用 `ESP32BASE_WEB_NATIVE_TEST=1` 使用测试 harness 设置 method、path、参数、body、认证/同源结果，并捕获状态码、headers、重定向和响应体；该接口只在非 ESP32 native 测试构建可用，详见 [Web 与配网](docs/04_web.md) 和 [API 契约](docs/03_api.md)。

Web/Auth 不再内置启用 admin/admin。启用 Web 的业务必须在 `Esp32Base::begin()` 前调用 `Esp32BaseWeb::setDefaultAuth(user, pass)`，或先通过已保存的 `eb_web` 认证启动；两者都不存在时 Web 服务 fail-closed，不注册 HTTP server。仅受控开发固件可显式打开 `ESP32BASE_WEB_ALLOW_INSECURE_DEFAULT_AUTH=1` 使用内置 `admin/admin` 兜底，并会输出 WARN 审计日志。示例中的固定账号仅用于本地 demo，业务项目不要照抄。WiFi 密码和 Web Auth 密码按项目选择明文保存到普通 NVS，并在 INFO 日志中明文输出；明文凭据日志是本库面向业务接入和现场调试的明确选择，不作为缺陷或待修风险评估。HTML、JSON 和 API 响应仍不输出 Web Auth 密码。Web Auth 保存会读回校验；保存失败不会主动清空旧认证，保存成功后才切换当前运行态认证。

业务首页推荐注册为 `/index`。在 `HOME_APP` 或 `HOME_COMBINED` 下，如果没有显式 `setHomePath()` 且存在 `/index` 业务页，裸 `/` 会跳转到 `/index`；如果应用确实需要直接渲染裸根路径，可显式 `setHomePath("/")` 并注册 GET `/` 业务页。`/esp32base` 始终保留为基础库系统入口。

启用 FS 的 profile 会默认启用系统诊断日志。它的实现/API 名称仍是 `Esp32BaseFileLog`，默认写入 `/esp32base/logs/system.log`，Web 默认入口显示为 `System Logs`，URL 仍是 `/esp32base/logs`。系统诊断日志面向开发、维护和运维排障，记录设备运行过程中的技术事实和内部链路，例如启动、reset reason、WiFi、NTP、OTA、LittleFS、FileLog 写入保护和基础库健康状态。默认容量 `4 × 32KB`，模式 ERROR；运行时可配置为 OFF、ERROR、WARN、INFO。默认 ERROR 是低 Flash 磨损策略：全功能固件在没有业务数据、应用事件或显式调试日志需求时，只把错误级系统诊断写入 LittleFS，避免 WARN/INFO 默认长期刷写影响寿命。慢 Web 操作和慢 loop 诊断属于 INFO 性能提示，默认 ERROR 系统日志不记录，现场排查时可切到 INFO 查看。长期离线或路由器不可用时，WiFi 慢速 backoff 的重复重试日志使用 INFO，默认 ERROR 系统日志不写入；现场排查可临时切到 INFO 查看完整重试链路。INFO 模式会让启动、联网、OTA、性能提示和维护操作等低优先级日志进入 LittleFS，适合调试和短期现场排查，不建议作为长期量产默认。Status 页默认只显示 LittleFS O(1) 容量摘要和 FileLog 配置摘要，不做全量文件树扫描，也不打开 FileLog 段文件统计已用大小；完整文件数量、目录数量、top files 和可读性检查位于 `/esp32base/fs`。低频分区表和运行 ELF SHA 只在 `/esp32base?details=1` 显示。System Logs 的 GET 页面只读取已落盘快照，不主动 flush 或写 LittleFS；缓存中的低优先级日志会按常规 flush interval 落盘。如果 FS 满或文件损坏导致写入失败，Web 会显示 FileLog 运行态为 `write fault`，表示配置仍开启、已有日志可能仍可读取，但新日志写入已被保护停写。`unavailable` 表示模式开启但当前 FS/init 状态无法启用系统日志；`disabled` 表示模式为 OFF，新日志不会写入。需要更详细现场诊断时，可通过 Web System 页或代码显式切到 WARN/INFO。

需要记录业务可解释事件时，可显式启用通用应用事件日志。它默认关闭，不随 FULL profile 自动开启；启用后依赖 FS，默认在 `/esp32base/app-events/events.bin` 保存 `1024` 条固定结构记录，约 `188 KiB`。App Events 是面向长期运行设备的“近期关键事件窗口”，用于解释最近一段业务行为；它不是业务长期数据模型，不替代统计、报表、累计量、传感器采样历史、完整执行历史或不允许覆盖的业务数据。每条 App Event 会写记录槽和 header，适合低频关键业务事件，不适合高频采样、流水或统计明细。应用项目负责决定事件何时写入、`source/type/reason/object` 如何命名以及页面文案如何解释；基础库只提供结构化写入、固定容量环形覆盖、分页读取、Web/API/CSV 展示和按需清空。

```ini
build_flags =
  -D ESP32BASE_PROFILE=ESP32BASE_PROFILE_FULL
  -D ESP32BASE_ENABLE_APP_EVENTS=1
```

内置入口为 `/esp32base/app-events`，JSON API 为 `/esp32base/api/app-events?offset=0&limit=50`，CSV 导出为 `/esp32base/app-events.csv`，清空入口位于 System 页的危险操作区。内置事件日志页面用偏底层视角展示事件日志文件、slot、状态、CRC 和完整记录字段；JSON API 只输出有效事件，适合业务系统创建自己的业务事件列表和详情页并用业务语言解释。页面/API/CSV 支持等级、时间类型、来源、类型、原因和关键词筛选；CSV 文本字段会做语法转义并防护常见 spreadsheet formula 前缀；时间优先显示可信真实时间，无法解析时显示 `uptime N ms` 和 boot id。单条损坏记录不会让整个日志停写；结构性 header/写入故障才进入 fault。格式化 LittleFS 后，System 页会重新创建 App Events store；FS 管理页会把 `/esp32base/app-events/events.bin` 标记为 `app events store` 作为提醒，但仍允许测试和维护时普通上传或删除，操作后会重新加载 App Events 运行态。该能力明确独立于 `/esp32base/logs` 的系统诊断日志，不混入 WiFi、OTA、NTP、启动、健康状态等基础库系统日志。样例见 `examples/app_events_demo`。

App Events 适合记录计划跳过、保护触发、运行异常、外部 API 决策、用户清除告警等低频关键事件。不适合记录用水量长期统计、报表数据、累计值、传感器采样序列、大 payload、高频明细或必须永久保留的完整业务历史；这些应继续由业务自己的数据模型和文件负责。

判定规则：System Diagnostic Logs（系统诊断日志，`Esp32BaseFileLog`）记录设备和基础库“内部发生了什么、技术原因是什么、排障链路在哪里”；App Events（应用业务事件，`Esp32BaseAppEventLog`）记录“对业务产生了什么影响、系统做出了什么业务决策、用户需要理解什么结果”。

| 能力 | 面向对象 | 应记录 | 不应记录 |
| --- | --- | --- | --- |
| System Logs / 系统诊断日志（`Esp32BaseFileLog`） | 开发、维护、运维排障 | boot/reset、WiFi、NTP、OTA、LittleFS、FileLog fault、基础库健康状态、内部错误链路 | 业务枚举、业务文案、业务报表或用户可解释的业务历史 |
| App Events / 应用业务事件（`Esp32BaseAppEventLog`） | 业务页面、现场用户、业务排查 | 业务决策、保护动作、跳过原因、业务告警、用户维护结果、外部决策结果 | 纯系统诊断、调试日志、高频采样、长期统计、完整业务数据模型 |

应用项目不要把 Esp32Base 系统事件写入 App Events，例如 boot/reset/restart reason、WiFi、NTP、OTA、LittleFS mount/write fault、FileLog fault、基础库健康状态等。同一个底层故障可以同时留下系统诊断日志和应用业务事件，但这是因为它跨越了两个语义层，而不是为了复制一条日志：系统诊断日志写技术事实和内部错误链路，应用业务事件写业务影响、保护动作、跳过原因、用户维护结果或外部决策结果。硬件或存储异常只有在导致业务保护、跳过、停机、业务告警或用户维护动作时，才以业务事件写入 App Events。

需要业务持久化参数配置页时，可启用 App Config。业务显式声明容量并在 `Esp32Base::begin()` 前注册分组和字段，基础库会在 System 页首位提供 `App Config` 入口：

```ini
build_flags =
  -D ESP32BASE_ENABLE_APP_CONFIG=1
  -D ESP32BASE_APP_CONFIG_MAX_GROUPS=3
  -D ESP32BASE_APP_CONFIG_MAX_FIELDS=8
```

App Config 支持 string、int、decimal 定点数、bool 和 enum 字段；保存时后端重新校验并只写入实际变化字段。需要只读模式、未来配置版本或业务版本不兼容时拒绝整页提交的场景，应使用 `setPageValidateCallback()` 做保存前 veto；该回调可读取本次提交的全部字段，返回 false 时不会写入任何 App Config NVS 字段。提交通过后多字段保存逐项写入 NVS，不提供跨字段事务；如果某个字段写入失败，会停止继续写后续字段并显示 partial，关键强一致业务配置应使用单个 POD/blob 或业务自定义提交模型。`setSaveCallback()` 只用于保存后通知，不用于拒绝保存。注册传入的字符串和 enum option 数组需保持固件生命周期有效。标记为重启后生效的字段保存后会在未重启会话内持续提示旧值和已保存新值，适合低频修改的小型业务配置。

Web 页面应优先使用 Esp32Base 的 UI baseline、helper 和页面能力块；不要在业务项目里为样式问题临时复制 CSS 或绕开基础库。单字段轻量编辑可使用行内编辑 helper，小型 1-3 字段表单可使用弹层表单 helper；二者会在支持 `fetch` 的浏览器中局部提交和局部替换，禁用 JS 时仍回退到普通表单提交。需要业务自控打开/关闭时，也可直接使用原生 `<dialog class="panel eb-modal">` 并复用 `fieldgrid`、`actions`、`btnlink`。找不到合适页面能力块时，先查看 [Web UI 页面结构与样式基线](docs/11_web_ui_baseline.md)，并优先回到 Esp32Base 评估是否补充统一能力。

需要真实时间时，业务项目应优先通过 `Esp32BaseTime::snapshot()` 获取统一时间快照。NTP 是最高优先级时间源；启用 RTC 时，DS3231 或 PCF8563 可在离线启动时提供真实时间。断网启动时业务仍能记录当前 `bootId + uptimeSec`；本次 boot 后续由 RTC 或 NTP 建立可信时间后，可用 `Esp32BaseTime::resolveCurrentBootEvent()` 只回填同一 boot 的相对时间事件，历史未知时间不会被伪造为日期。旧的 `Esp32BaseNtp::snapshot()` 仍表示 NTP 自身状态，不作为 RTC-only 设备的业务时间入口。

外部 RTC 通过 `ESP32BASE_ENABLE_RTC=1` 显式启用，当前支持 `ESP32BASE_RTC_DRIVER_DS3231` 和 `ESP32BASE_RTC_DRIVER_PCF8563`。同一个应用固件只选择一个驱动，不做运行时自动识别；硬件板确定后在 `platformio.ini` 中设置 `ESP32BASE_RTC_DRIVER`。默认 I2C 地址可用 `ESP32BASE_RTC_I2C_ADDR=0` 交给驱动选择：DS3231 为 `0x68`，PCF8563 为 `0x51`。基础库默认不调用 `Wire.begin()`，推荐业务在 `Esp32Base::begin()` 前初始化自己的 I2C 总线并调用 `Esp32BaseRtc::configure(Wire)`；如果希望基础库初始化 RTC I2C，可设置 `ESP32BASE_RTC_AUTO_WIRE_BEGIN=1`，并按需配置 `ESP32BASE_RTC_SDA`、`ESP32BASE_RTC_SCL` 和 `ESP32BASE_RTC_I2C_CLOCK_HZ`。RTC 中断、闹钟、方波、温度等芯片扩展能力不由基础库占用，业务项目可在同一 I2C 总线上自行访问；基础库只做低频读写时间、状态展示和 NTP 成功后的可选写回。示例见 `examples/rtc_time_source`。

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

系统诊断日志（实现/API 名称 `Esp32BaseFileLog`）是 Runtime/FS 能力，不属于 Core。`CORE` 和默认 `NET` 不链接 LittleFS；`RUNTIME`、`NET_RUNTIME`、`WEB_RUNTIME`、`FULL` 默认可用系统诊断日志。

仓库示例默认面向 ESP32 4MB Flash，并使用 `partitions/esp32-4mb-ota-balanced.csv`。应用项目强烈推荐直接选择 `partitions/` 中已有分区表，不要在业务项目里重新设计分区布局；除非硬件容量、OTA 策略或持久化容量确实不匹配，才自定义分区表，并必须同步验证串口烧录、OTA、NVS、LittleFS 和数据保留边界。

推荐分区表：

| 文件 | 适用场景 | NVS | OTA state | 单 app slot（最大固件） | LittleFS（最大文件数据） | coredump | 关键要求 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `partitions/esp32-4mb-ota-balanced.csv` | classic ESP32 4MB 默认双 OTA，固件体积正常，保留较大 LittleFS | `20 KB / 0x5000` | `8 KB / 0x2000` | `1.25 MB / 0x140000` | `1.38 MB / 0x160000` | `64 KB / 0x10000` | `app0=0x10000`，兼容 PlatformIO/Arduino 默认上传偏移 |
| `partitions/esp32-4mb-ota-large-app.csv` | classic ESP32 4MB 双 OTA，大 Web/FULL 业务固件，需要更大 app slot | `20 KB / 0x5000` | `8 KB / 0x2000` | `1.38 MB / 0x160000` | `1.13 MB / 0x120000` | `64 KB / 0x10000` | `app0=0x10000`，兼容 PlatformIO/Arduino 默认上传偏移 |
| `partitions/esp32-4mb-ota-large-fs.csv` | classic ESP32 4MB 双 OTA，固件较小但记录、日志或文件数据较多 | `20 KB / 0x5000` | `8 KB / 0x2000` | `1.00 MB / 0x100000` | `1.88 MB / 0x1E0000` | `64 KB / 0x10000` | `app0=0x10000`，适合非 FULL 或页面较少的应用 |
| `partitions/esp32-c3-4mb-ota-balanced.csv` | ESP32-C3 4MB 双 OTA | `20 KB / 0x5000` | `8 KB / 0x2000` | `1.25 MB / 0x140000` | `1.38 MB / 0x160000` | `64 KB / 0x10000` | C3 项目优先使用 |
| `partitions/esp32-s3-8mb-ota-balanced.csv` | ESP32-S3 8MB 双 OTA，较宽松 app/FS 空间 | `20 KB / 0x5000` | `8 KB / 0x2000` | `2.25 MB / 0x240000` | `3.38 MB / 0x360000` | `64 KB / 0x10000` | S3 8MB 项目优先使用 |

`Esp32BaseFs` 对业务暴露文本、二进制、追加、定长文件、目录和容量 API，并提供 `readBytesAt()` / `writeBytesAt()` 按偏移读写能力。业务可通过这些 API 实现二进制定长日志分页读取和环形覆盖写入，不需要 include `LittleFS.h` 或 Arduino `File`。需要同时读取总容量和已用容量时优先使用 `storageInfo(total, used)`，避免分别调用 `totalBytes()` / `usedBytes()` 造成重复底层查询。大块读写会在 Esp32BaseFs 层分块并定期让出调度，Watchdog 启用时会配合基础库长操作机制，业务不需要在每个存储类里重复拆分 Flash 写入。固定容量环形文件应先用 `createFixedFile()` 确保容量，再用 `writeBytesAt()` 覆盖槽位；`appendBytes()` 适合低频追加，会做写后大小和末端可读校验，不适合作为大文件 bulk 初始化循环。业务必须检查这些 API 的返回值；当 LittleFS 元数据仍声明文件大小但内容块不可读、FS 满或写后校验失败时，API 会返回 `false`，且写入失败不等于已回滚。LittleFS mount failed 时不会自动格式化，以保护业务持久化文件；需要清空或重建文件系统时，只能通过明确的维护动作调用 `Esp32BaseFs::format()` 或 Web System 页格式化入口。Web System 页的 `format-fs` 只清 LittleFS，不清 WiFi、Web Auth、业务 namespace 或任何 NVS 配置；显式工具页格式化成功后可通过 `Esp32BaseWeb::setAfterFormatFsCallback()` 或 `Esp32BaseWeb::EVENT_TOOLS_FORMAT_FS_SUCCESS` 通知应用，业务如需同步清 NVS 统计、文件索引或缓存，应在该回调/事件中自行处理。内置 `/esp32base/fs` 会显示文件树的 Last modified 和 Status；Last modified 来自 LittleFS 最后修改时间，不是创建时间，时间明显不可信时显示 `unknown`。不可读文件会标记为 `unreadable`，只在管理模式提供删除，不提供下载。`/esp32base/fs?manage=1` 还提供受限上传，用于测试期导入业务数据文件：上传保留本地文件名，可选择任何已有目录，不创建目录；同名文件会在浏览器确认后覆盖；上传先写同目录唯一临时文件，校验通过后再替换目标，避免上传中断时先清空旧文件；断电遗留的临时文件不会阻塞下一次上传，可由管理入口显式删除。

LittleFS 中的 `/esp32base/**` 是基础库管理命名空间。系统诊断日志默认在 `/esp32base/logs/system.log`，App Events store 默认在 `/esp32base/app-events/events.bin`；业务项目应把自己的文件放到 `/app/**`、`/data/**` 或项目自定义目录，避免和基础库维护动作混用。

`ESP32BASE_PROFILE_FULL` 默认同时支持 Web OTA 和 PlatformIO/espota 命令行 OTA。命令行 OTA 使用 `Esp32Base::hostname()` 对应的 `<hostname>.local`、标准端口 3232，以及当前 Web Auth 密码：

```ini
upload_protocol = espota
upload_port = esp32-demo.local
upload_flags =
  --auth=<current-web-auth-password>
```

不需要命令行 OTA 的业务可显式设置 `ESP32BASE_ENABLE_ARDUINO_OTA=0`，现有 Web OTA 不受影响。

需要更快的命令行上传时，可使用 Esp32Base 内置 `webota` target。它复用现有 HTTP Web OTA 接口和 Web Auth，推荐直接配置设备 IP：

```ini
extra_scripts =
  post:path/to/Esp32Base/scripts/esp32base_webota.py

custom_esp32base_webota_host = 192.168.2.112
custom_esp32base_webota_user = <current-web-auth-user>
custom_esp32base_webota_password = <current-web-auth-password>
```

```sh
pio run -t webota
```

`espota` 是 ArduinoOTA 标准协议；`webota` 是 Esp32Base 提供的 HTTP 上传方式，默认使用 `/esp32base/ota/raw` raw binary 路径和 64KB 分块，并在上传前预检认证、RSSI 和 OTA 目标分区容量，超出时不发送固件 body。raw 上传会根据弱 RSSI 自动把发送分块和节奏降到更保守的默认值，也可通过 `esp32base_webota_chunk_size`、`esp32base_webota_raw_pause_ms` 和 `esp32base_webota_socket_send_buffer` 显式覆盖；raw 传输中断时不会自动改走 `/esp32base/ota` multipart 路径。脚本会为 raw HTTP body 增加传输 padding，设备端只写入 `X-Firmware-Size` 声明的真实固件字节，以避免 Arduino `HTTPRaw` 最后一包短读等待超时。浏览器手工选择文件上传仍使用 `/esp32base/ota` multipart 表单路径，页面会在选择文件后立即显示固件大小，并在发送前按当前 OTA 目标 slot 容量拦截过大的固件，不受脚本默认值影响。详细配置见 `docs/05_ota.md`。

双 OTA 设备如果已经通过 Web OTA 切换到另一个槽位，串口 `pio run -t upload` 的行为取决于分区表和 PlatformIO/Arduino flash plan。仓库标准分区中 `otadata=0xe000`，串口上传会把 `boot_app0.bin` 写到同一位置，通常会把启动选择重新初始化到 `ota_0`；但自定义分区如果把 `otadata` 移到其他偏移，串口上传可能只覆盖固定 app 槽而不更新当前启动槽，设备仍可能继续从旧的 `ota_1` 启动。串口恢复优先使用基础库脚本，它会按分区表写入两个 OTA app 槽，并在同一次 `write_flash` 里写入全 `0xFF` 的 `otadata` 镜像完成清理；清理 `otadata` 时脚本会跳过 `boot_app0`，以避免重复或错误写入：

```sh
python path/to/Esp32Base/scripts/esp32base_serial_recover_ota.py \
  -d path/to/business/project \
  -e esp32dev \
  --port /dev/ttyUSB0
```

可先加 `--dry-run` 查看将执行的 `esptool.py` 命令和 `otadata` 处理方式；特殊分区表或板型可显式覆盖 bootloader、partition table 和 boot_app0 偏移。默认情况下 `boot_app0` 跟随 `data/ota` 偏移，且在清理 `otadata` 时跳过。执行恢复前先关闭 `pio device monitor`、串口调试器和其他占用同一端口的程序；如果 CH340/CP210x 转串口在高波特率下出现 `Serial data stream stopped` 或连接中断，可用 `--baud 115200` 降速重试。

## 文档入口

建议按顺序阅读：

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
