# Esp32Base

`Esp32Base` 是面向 ESP32 / ESP32-S3 / ESP32-C3、基于 Arduino ESP32 Core 的轻量基础库设计。

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

业务记录的职责边界、磁盘格式、容量规划、断电恢复和经典 ESP32 实机性能基线详见 [Record Store 设计、接入与实机基准](docs/12_record_store.md)。

Web 页面结构、样式基线、业务页面模式和换肤策略详见 [Web UI 页面结构与样式基线](docs/11_web_ui_baseline.md)。
业务项目接入前，建议先用 `examples/web_ui_gallery` 统一查看和验证状态、记录、配置、命令、分步操作、确认和空状态等页面样式；`examples/full_demo` 侧重完整功能集成。
基础 Web CSS 由 `/esp32base/ui.css` 统一输出并允许浏览器缓存，业务页面通过 `sendHeader()` 自动引用，不需要复制样式；按钮按轻量设备控制台风格收敛，明确保存/执行动作和普通入口保持清楚层级；原生 `<dialog>` 可复用基础弹层、表单和按钮样式。

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

启用 WiFi 的 profile 默认启用物理恢复热点：ESP32 / ESP32-S3 使用 GPIO0，ESP32-C3 使用 GPIO9。应用运行后低电平按住默认10秒会立即切换到配置热点（通常为 `192.168.4.1`），保留原 WiFi 凭据且不重启；默认BOOT键如果在复位或上电期间保持按下，仍会按芯片硬件规则进入ROM下载模式。System 页面可立即修改启用状态、GPIO和长按秒数；配置独立保存在 `eb_wifi_rcv`，业务必须确认所选引脚未被板载外设、Flash、PSRAM、USB或应用硬件占用。

示例工程默认使用 `-fno-exceptions` 和 `-flto` 减少 Arduino / C++ 运行时与未使用库代码的链接体积，并移除 PlatformIO 默认的 `-fno-lto`；Core 3.x 示例还移除 `-fuse-cxa-atexit`，避免固件不会用到的进程退出析构注册开销。Esp32Base 与示例代码不使用 C++ `throw/catch`，也不依赖进程退出析构；业务应用如果确实依赖 C++ 异常、特殊链接行为或退出析构语义，应从自己的构建配置中移除对应 flags 并重新评估容量。

底部横条可在 System 页面配置为 Off、Status only 或 Links + status。该设置保存到 `eb_ui.footer_mode`，用于控制 `sendFooter()` 输出的紧凑系统入口和运行摘要。

Web 应用路由默认容量统一为 24。该上限只覆盖业务 `addRoute()` / `addPage()` / `addApi()` 注册的静态路由表；内置 Web 路由不占用此表。route 较少且需要节省静态 RAM 的应用，可在完成 route 数量核算后通过构建参数显式调小 `ESP32BASE_WEB_MAX_ROUTES`。业务 CSS/JS/图片等固定内容应优先用 `Esp32BaseWeb::addStaticAsset()` 注册到基础库 WebServer；该能力不占用应用 route 表，默认带 Basic Auth、固定 `Content-Length`、`nosniff` 和可配置浏览器缓存，避免业务项目绕开 WebServer 或复制静态资源发送逻辑。

应用项目需要在 native 单元测试中直接执行 Web handler 时，可启用 `ESP32BASE_WEB_NATIVE_TEST=1` 使用测试 harness 设置 method、path、参数、body、认证/同源结果，并捕获状态码、headers、重定向和响应体；该接口只在非 ESP32 native 测试构建可用，详见 [Web 与配网](docs/04_web.md) 和 [API 契约](docs/03_api.md)。

Web/Auth 不再内置启用 admin/admin。启用 Web 的业务必须在 `Esp32Base::begin()` 前调用 `Esp32BaseWeb::setDefaultAuth(user, pass)`，或先通过已保存的 `eb_web` 认证启动；两者都不存在时 Web 服务 fail-closed，不注册 HTTP server。仅受控开发固件可显式打开 `ESP32BASE_WEB_ALLOW_INSECURE_DEFAULT_AUTH=1` 使用内置 `admin/admin` 兜底，并会输出 WARN 审计日志。示例中的固定账号仅用于本地 demo，业务项目不要照抄。WiFi 密码和 Web Auth 密码当前保存到普通 NVS；未启用平台级 flash encryption 时，不应把设备物理存储视为密文凭据库。日志、HTML、JSON 和 API 响应不输出密码值，只输出用户名、SSID、来源、结果或 `password_set` 这类状态。Web Auth 保存会读回校验；保存失败不会主动清空旧认证，保存成功后才切换当前运行态认证。

业务首页推荐注册为 `/index`。在 `HOME_APP` 或 `HOME_COMBINED` 下，如果没有显式 `setHomePath()` 且存在 `/index` 业务页，裸 `/` 会跳转到 `/index`；如果应用确实需要直接渲染裸根路径，可显式 `setHomePath("/")` 并注册 GET `/` 业务页。只有裸 `/` 受 `HomeMode` 和 `configuredHomePath()` 控制；`/esp32base/status` 是 Status 页规范地址，`/esp32base` 与 `/esp32base/` 只负责重定向到该地址。System 设置与维护页固定为 `/esp32base/system`。即使底部横条关闭，Status 页正文仍保留 System 入口。

启用 FS 的 profile 会默认启用系统诊断日志。实现/API 名称仍是 `Esp32BaseFileLog`，默认写入 `/esp32base/logs/system.log`，Web 入口为 `/esp32base/logs`，显示名为 `System Logs`。它面向开发、维护和运维排障，默认容量 `4 × 32KB`、模式 ERROR；运行时可切换 OFF、ERROR、WARN、INFO。Status 页按 Network、Runtime、Persistence、Firmware & OTA、Platform & Security 汇总运行态，并显示通过镜像元数据低成本取得的当前固件大小；完整文件树在 `/esp32base/fs`，低频网络、NVS、分区表和运行 ELF SHA 只在 `/esp32base/status?details=1` 显示。页面不创建后台采样任务或自动刷新，关闭后没有持续运行负担。FS 不可用或写入失败时会显示 `unavailable`、`disabled` 或 `write fault` 等运行态。

需要长期保存浇水、开关门、喂食等固定结构业务历史时，可启用 `ESP32BASE_ENABLE_RECORD_STORE=1`，为每种固定负载创建独立的 `Esp32BaseRecordStore`。应用负责用定宽整数、代码、位标志和固定位置把业务对象编码为固定字节负载；基础库统一添加32位自增ID、完成时间、启动标识、运行时间、持续时间和CRC32，并负责按段追加、容量轮换、断电恢复、最新优先分页、按ID读取、状态和逻辑清空。启用Web的项目在Store完成 `begin()` 后登记当前版本对象，System页便统一提供状态、`Clear Business Records` 和格式化后的Store恢复，业务不再自建清空入口。登记只要求对象随后持续有效，不限定必须采用全局变量；同一Store的操作需要串行，推荐集中在loop/system task。每个版本使用 `/esp32base/records/<record-type>.v<version>/` 独立目录，未登记历史版本不属于该操作，最大逻辑存储预算由业务按LittleFS分区和保留需求确定。示例见 `examples/record_store_demo`。

需要记录业务可解释事件时，可显式启用 App Events。它默认关闭，不随 FULL profile 自动开启；启用后复用 Record Store，并在业务 Store 前优先创建 `/esp32base/records/app-events.v2/`。默认最大逻辑预算为100 KiB，24字节事件负载加24字节通用元数据后每条48字节，估算容量2113条；按约4 KiB完整段轮换时实际保留约2029～2113条。App Events用于解释近期业务决策和影响；完整浇水、开关门、喂食历史仍应使用业务自己的RecordStore。事件保存 `eventCode/reasonCode/objectId/value1/value2/flags/level/eventKind/conditionId` 定宽数值字段，显示文字由业务代码映射。

```ini
build_flags =
  -D ESP32BASE_PROFILE=ESP32BASE_PROFILE_FULL
  -D ESP32BASE_ENABLE_APP_EVENTS=1
```

条件跟踪默认随 App Events 开启。离散动作调用 `appendDiscreteEvent()`，每次调用都独立保存；周期检测到的持续故障使用应用长期持有的 `ConditionStateTracker` 调用 `observeConditionState()`，只在确认生效和确认恢复时写事件及一个 `eb_app_events.active_id_bits` NVS整数。确认时间只判断应用连续上报的状态，不负责轮询硬件。条件ID范围固定为1～32，并且是跨重启、跨保留NVS固件升级的持久化schema；同一ID不能在未清理旧状态时改成另一种业务含义。如项目只需离散事件，可显式设置 `ESP32BASE_ENABLE_APP_EVENT_CONDITIONS=0`，不会链接条件跟踪的NVS和状态机代码。

内置入口为 `/esp32base/app-events`，JSON API 为 `/esp32base/api/app-events?offset=0&limit=50`，CSV 导出为 `/esp32base/app-events.csv`，清空入口位于 System 页危险操作区。页面/API/CSV 支持等级、时间类型、事件码和原因码筛选；损坏记录不会返回，只在状态中报告。该能力独立于 `/esp32base/logs` 的系统诊断日志。样例见 `examples/app_events_demo`。

App Events 适合记录开门受阻、喂食因缺料跳过、浇水因缺水停止等低频关键业务事件。不适合记录长期统计、报表数据、累计值、传感器采样序列、大 payload、高频明细或必须长期分页读取的完整业务历史；这些应使用业务自己的 RecordStore 或其他明确的数据模型。

判定规则：System Diagnostic Logs（系统诊断日志，`Esp32BaseFileLog`）记录设备和基础库“内部发生了什么、技术原因是什么、排障链路在哪里”；App Events（应用业务事件，`Esp32BaseAppEvents`）记录“对业务产生了什么影响、系统做出了什么业务决策、用户需要理解什么结果”；RecordStore保存可长期分页读取的完整业务历史事实。

| 能力 | 面向对象 | 应记录 | 不应记录 |
| --- | --- | --- | --- |
| System Logs / 系统诊断日志（`Esp32BaseFileLog`） | 开发、维护、运维排障 | boot/reset、WiFi、NTP、OTA、LittleFS、FileLog fault、基础库健康状态、内部错误链路 | 业务枚举、业务文案、业务报表或用户可解释的业务历史 |
| App Events / 应用业务事件（`Esp32BaseAppEvents`） | 业务页面、现场用户、业务排查 | 业务决策、保护动作、跳过原因、业务告警、用户维护结果、外部决策结果 | 纯系统诊断、调试日志、高频采样、完整浇水/开关门/喂食历史 |
| Business Records（`Esp32BaseRecordStore`） | 业务历史页、详情和导出 | 固定结构的完整浇水、开关门、喂食等历史事实 | 系统诊断文本、字段查询、事务或对象映射 |

应用项目不要把 Esp32Base 系统事件写入 App Events，例如 boot/reset/restart reason、WiFi、NTP、OTA、LittleFS mount/write fault、FileLog fault、基础库健康状态等。同一个底层故障可以同时留下系统诊断日志和应用业务事件，但这是因为它跨越了两个语义层，而不是为了复制一条日志：系统诊断日志写技术事实和内部错误链路，应用业务事件写业务影响、保护动作、跳过原因、用户维护结果或外部决策结果。硬件或存储异常只有在导致业务保护、跳过、停机、业务告警或用户维护动作时，才以业务事件写入 App Events。

需要业务持久化参数配置页时，可启用 App Config。业务显式声明容量并在 `Esp32Base::begin()` 前注册分组和字段，基础库会在 System 页首位提供 `App Config` 入口：

```ini
build_flags =
  -D ESP32BASE_ENABLE_APP_CONFIG=1
  -D ESP32BASE_APP_CONFIG_MAX_GROUPS=3
  -D ESP32BASE_APP_CONFIG_MAX_FIELDS=8
```

App Config 支持 string、int、decimal 定点数、bool 和 enum 字段；保存时后端重新校验并只写入实际变化字段。`setPageValidateCallback()` 用于保存前 veto，`setSaveCallback()` 只用于保存后通知。多字段保存不是跨 key 事务，关键强一致配置应使用单个 POD/blob 或业务自定义提交模型。注册传入的字符串和 enum option 数组需保持固件生命周期有效。标记为重启后生效的字段会在未重启会话内持续提示运行中旧值和已保存新值。

Web 页面可优先使用 Esp32Base 的 UI baseline、helper 和页面能力；不要在业务项目里为样式问题临时复制 CSS 或绕开基础库壳层。单字段轻量编辑可使用行内编辑 helper，小型 1-3 字段表单可使用弹层表单 helper；二者会在支持 `fetch` 的浏览器中局部提交和局部替换，禁用 JS 时仍回退到普通表单提交。需要业务自控打开/关闭时，也可直接使用原生 `<dialog>` 并复用基础表单和按钮样式。现有页面能力不适合时，业务侧可以做最小局部实现；多个项目确实会复用的模式，再回到 Esp32Base 补通用 helper、样式、示例和文档。

需要真实时间时，业务项目应优先通过 `Esp32BaseTime::snapshot()` 获取统一时间快照。NTP 是最高优先级时间源；启用 RTC 时，DS3231 或 PCF8563 可在离线启动时提供真实时间。断网启动时业务仍能记录当前 `bootId + uptimeSec`；本次 boot 后续由 RTC 或 NTP 建立可信时间后，可用 `Esp32BaseTime::resolveCurrentBootEvent()` 只回填同一 boot 的相对时间事件，历史未知时间不会被伪造为日期。旧的 `Esp32BaseNtp::snapshot()` 仍表示 NTP 自身状态，不作为 RTC-only 设备的业务时间入口。

外部 RTC 通过 `ESP32BASE_ENABLE_RTC=1` 显式启用，当前支持 `ESP32BASE_RTC_DRIVER_DS3231` 和 `ESP32BASE_RTC_DRIVER_PCF8563`。同一个应用固件只选择一个驱动，不做运行时自动识别；硬件板确定后在 `platformio.ini` 中设置 `ESP32BASE_RTC_DRIVER`。默认 I2C 地址可用 `ESP32BASE_RTC_I2C_ADDR=0` 交给驱动选择：DS3231 为 `0x68`，PCF8563 为 `0x51`。基础库默认不调用 `Wire.begin()`，推荐业务在 `Esp32Base::begin()` 前初始化自己的 I2C 总线并调用 `Esp32BaseRtc::configure(Wire)`；如果希望基础库初始化 RTC I2C，可设置 `ESP32BASE_RTC_AUTO_WIRE_BEGIN=1`，并按需配置 `ESP32BASE_RTC_SDA`、`ESP32BASE_RTC_SCL` 和 `ESP32BASE_RTC_I2C_CLOCK_HZ`。RTC 芯片寄存器按 UTC 日历字段存储；显示和日志按 `ESP32BASE_NTP_GMT_OFFSET_SEC` / `ESP32BASE_NTP_DAYLIGHT_OFFSET_SEC` 固定偏移格式化，默认 UTC+8。RTC 中断、闹钟、方波、温度等芯片扩展能力不由基础库占用，业务项目可在同一 I2C 总线上自行访问；基础库只做低频读写时间、状态展示和 NTP 成功后的可选写回。示例见 `examples/rtc_time_source`。

RS485 半双工基础串口通过 `ESP32BASE_ENABLE_RS485_PORT=1` 显式启用。`Esp32BaseRs485Port` 只封装 ESP32 `HardwareSerial`、RX/TX/DE 引脚、baud、串口配置、发送前后 DE 方向切换、`flush()` 等待和轮询读取；它不包含 Modbus/RTU、CRC、地址、重试、超时帧解析或任何应用协议。业务协议应在应用层基于 `writeBytes()`、`readable()` 和 `readByte()` 自行实现。示例见 `examples/rs485_port`。

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

仓库示例默认面向 ESP32 4MB Flash，并使用 `partitions/esp32-4mb-ota-balanced.csv`。应用项目强烈推荐直接选择 `partitions/` 中已有分区表，不要在业务项目里重新设计分区布局；除非硬件容量、OTA 策略或持久化容量确实不匹配，才自定义分区表，并必须同步验证串口烧录、OTA、NVS、LittleFS 和数据保留边界。ESP32 / ESP32-C3 4MB 推荐分区表保留两个 1.5MB OTA app slot，剩余空间作为 LittleFS 数据分区。

推荐分区表：

| 文件 | 适用场景 | NVS | OTA state | 单 app slot（最大固件） | LittleFS（最大文件数据） | coredump | 关键要求 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `partitions/esp32-4mb-ota-balanced.csv` | classic ESP32 4MB 默认双 OTA，两个 1.5MB 固件槽，剩余空间用于 LittleFS | `20 KB / 0x5000` | `8 KB / 0x2000` | `1.50 MB / 0x180000` | `896 KB / 0xE0000` | `64 KB / 0x10000` | `app0=0x10000`，兼容 PlatformIO/Arduino 默认上传偏移 |
| `partitions/esp32-c3-4mb-ota-balanced.csv` | ESP32-C3 4MB 双 OTA，两个 1.5MB 固件槽，剩余空间用于 LittleFS | `20 KB / 0x5000` | `8 KB / 0x2000` | `1.50 MB / 0x180000` | `896 KB / 0xE0000` | `64 KB / 0x10000` | C3 项目优先使用 |
| `partitions/esp32-s3-8mb-ota-balanced.csv` | ESP32-S3 8MB 双 OTA，较宽松 app/FS 空间 | `20 KB / 0x5000` | `8 KB / 0x2000` | `2.25 MB / 0x240000` | `3.38 MB / 0x360000` | `64 KB / 0x10000` | S3 8MB 项目优先使用 |

`Esp32BaseFs` 对业务暴露文本、二进制、追加、定长文件、目录、容量和按偏移读写 API，业务不需要直接 include `LittleFS.h` 或 Arduino `File`。容量读取优先使用 `storageInfo(total, used)`；固定容量文件先用 `createFixedFile()`，再用 `writeBytesAt()` 覆盖槽位；`appendBytes()` 适合低频追加。LittleFS mount failed 时不会自动格式化，需要清空或重建时只能通过明确维护动作调用 `Esp32BaseFs::format()` 或 Web System 页格式化入口。Web 格式化只清 LittleFS，不清 WiFi、Web Auth、业务 namespace 或 NVS 配置；业务如需同步清理，应使用 after-format callback 或事件自行处理。`/esp32base/fs?manage=1` 提供受限上传，上传保留本地文件名，只能选择已有目录；覆盖上传应避免中断时先清空旧文件。

LittleFS 中的 `/esp32base/**` 是基础库管理命名空间。系统诊断日志默认在 `/esp32base/logs/system.log`，App Events store 默认在 `/esp32base/records/app-events.v2/`；业务项目自己的 RecordStore 也由基础库放到 `/esp32base/records/**`，其他业务文件应放到 `/app/**`、`/data/**` 或项目自定义目录，避免和基础库维护动作混用。

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

`espota` 是 ArduinoOTA 标准协议；`webota` 是 Esp32Base 提供的 HTTP 上传方式，默认使用 `/esp32base/ota/raw` raw binary 路径和 64KB 分块，并在上传前预检认证、RSSI 和 OTA 目标分区容量，超出时不发送固件 body。raw 上传会根据弱 RSSI 自动把发送分块和节奏降到更保守的默认值，也可通过 `esp32base_webota_chunk_size`、`esp32base_webota_raw_pause_ms` 和 `esp32base_webota_socket_send_buffer` 显式覆盖；raw 传输中断时不会自动改走 `/esp32base/ota` multipart 路径。脚本会为 raw HTTP body 增加传输 padding，设备端只写入 `X-Firmware-Size` 声明的真实固件字节，以避免 Arduino `HTTPRaw` 最后一包短读等待超时。设备端把 raw 接收块直接交给 Arduino `Update` 内部缓冲，不再额外申请连续 64 KB heap。浏览器手工选择文件上传仍使用 `/esp32base/ota` multipart 表单路径，页面会在选择文件后立即显示固件大小，并在发送前按当前 OTA 目标 slot 容量拦截过大的固件，不受脚本默认值影响。详细配置见 `docs/05_ota.md`。

量产项目建议启用 `ESP32BASE_OTA_REQUIRE_MARK_VALID=1`，避免 Arduino core 在应用自检前过早把新 OTA 镜像标记为 valid。业务应在传感器、配置和核心任务初始化通过后调用 `Esp32BaseOta::markCurrentValid()`；setup 阶段崩溃交给 bootloader rollback，运行期未确认交给 `Esp32BaseOta::handle()` timeout rollback。最小配置和示例见 `docs/05_ota.md`。

双 OTA 设备如果已经通过 Web OTA 切换到另一个槽位，串口 `pio run -t upload` 的行为取决于分区表和 PlatformIO/Arduino flash plan。仓库标准分区通常会把启动选择重新初始化到 `ota_0`；但自定义分区如果只覆盖固定 app 槽而不更新 OTA data，设备仍可能继续从旧槽启动。串口恢复优先使用基础库脚本，它会按分区表写入两个 OTA app 槽并清理启动选择：

```sh
python path/to/Esp32Base/scripts/esp32base_serial_recover_ota.py \
  -d path/to/business/project \
  -e esp32dev \
  --port /dev/ttyUSB0
```

可先加 `--dry-run` 查看将执行的 `esptool.py` 命令和 `otadata` 处理方式；特殊分区表或板型可显式覆盖 bootloader、partition table 和 boot_app0 偏移。执行恢复前先关闭 `pio device monitor`、串口调试器和其他占用同一端口的程序；如果 CH340/CP210x 转串口在高波特率下出现 `Serial data stream stopped` 或连接中断，可用 `--baud 115200` 降速重试。

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
