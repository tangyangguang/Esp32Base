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

新业务项目接入和旧 Profile/OTA 项目的一次性代码适配，先阅读 [应用接入与版本适配](docs/13_integration_and_upgrade.md)。已知限制、明确不支持能力和风险边界详见 [已知限制](docs/10_known_limitations.md)。

业务记录的职责边界、磁盘格式、容量规划、断电恢复和经典 ESP32 实机性能基线详见 [Record Store 设计、接入与实机基准](docs/12_record_store.md)。

Web 页面结构、样式基线、业务页面模式和换肤策略详见 [Web UI 页面结构与样式基线](docs/11_web_ui_baseline.md)。本项目采用 [MIT License](LICENSE)。
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
#define ESP32BASE_PROFILE ESP32BASE_PROFILE_LOCAL
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

启用 FS 的 profile 会默认启用系统诊断日志。实现/API 名称仍是 `Esp32BaseFileLog`，默认写入 `/esp32base/logs/system.log`，Web 入口为 `/esp32base/logs`，显示名为 `System Logs`。它面向开发、维护和运维排障，默认容量 `4 × 32KB`、模式 ERROR；运行时可切换 OFF、ERROR、WARN、INFO。Status 页按 Network、Runtime、Persistence、Firmware & OTA、Platform & Security 汇总运行态，并直接显示当前固件大小、网络参数、NVS 摘要和运行 ELF SHA；完整文件树仍位于 `/esp32base/fs`。页面不创建后台采样任务或自动刷新，所有状态只在页面请求时读取，关闭后没有持续运行负担。FS 不可用或写入失败时会显示 `unavailable`、`disabled` 或 `write fault` 等运行态。

需要长期保存浇水、开关周期、接触器运行区间等固定结构业务事实时，可启用 `ESP32BASE_ENABLE_RECORD_STORE=1`，为每种主要记录类型创建一个独立的 `Esp32BaseRecordStore`。应用负责定宽payload及其业务Schema；基础库统一添加ID、时间、持续时间和CRC，并负责顺序追加、分段轮转、掉电尾部恢复、分页读取和逻辑清空。每个版本使用 `/esp32base/records/<record-type>.v<version>/` 独立目录。

所有Store在 `begin()` 成功后通过 `Esp32BaseStorage::registerRecordStore()` 登记。`Esp32BaseStorage` 是LittleFS协调层：最多登记8个Store，默认限制所有业务Store合计逻辑预算为512 KiB，校验FileLog预算和至少 `max(128 KiB, 分区容量/4)` 的安全余量，统一提供受管路径保护、非受管文件可写容量、Store清空、格式化/重新挂载/组件恢复以及OTA期间写暂停。它只协调访问、容量和生命周期，不理解或混合FileLog、RecordStore及业务文件的数据格式。System页和FS上传均使用该协调结果；业务不再向Web模块登记Store。

持续异常的当前状态使用独立的 `Esp32BaseConditions`，不再建立通用应用事件数据库：

```ini
build_flags =
  -D ESP32BASE_ENABLE_RECORD_STORE=1
  -D ESP32BASE_ENABLE_CONDITIONS=1
```

应用长期持有 `ConditionTracker(conditionId, activationMs, recoveryMs)`，周期调用 `observe()`。`conditionId` 固定为1～32并持久化在 `eb_conditions.active_bits` 单个NVS位图中；Unknown会取消尚未确认的转换。只有NVS提交成功才返回 `Activated` 或 `Recovered`，应用可在收到这两个结果后自行决定是否向紧凑审计Store追加业务记录。Conditions只保存当前活动集合，没有LittleFS历史、内置历史页面或隐式业务Schema。

| 能力 | 职责 |
| --- | --- |
| `Esp32BaseFileLog` | 基础库和设备技术诊断，独立轮转，不作为业务历史上传 |
| `Esp32BaseRecordStore` | 每种主要业务事实的独立定长Store |
| `Esp32BaseConditions` | 少量持续异常的当前活动状态和确认状态机 |
| `Esp32BaseStorage` | 统一LittleFS访问、容量、受管路径和维护生命周期 |

同一Store的操作需要串行，推荐实时任务只投递轻量消息，由loop/system task完成持久化。关键业务事实应逐条完成追加、flush/close和读回验证；不要用批量缓存扩大掉电丢失窗口。示例见 `examples/record_store_demo`。

需要业务持久化参数配置页时，可启用 App Config。业务显式声明容量并在 `Esp32Base::begin()` 前注册分组和字段，基础库会在 System 页首位提供 `App Config` 入口：

```ini
build_flags =
  -D ESP32BASE_ENABLE_APP_CONFIG=1
  -D ESP32BASE_APP_CONFIG_MAX_GROUPS=3
  -D ESP32BASE_APP_CONFIG_MAX_FIELDS=8
```

App Config 支持 string、int、decimal 定点数、bool 和 enum 字段；保存时后端重新校验并只写入实际变化字段。`setPageValidateCallback()` 用于保存前 veto，`setSaveCallback()` 只用于保存后通知。System 页危险操作区提供 `Restore App Config Defaults`：二次确认后只删除已注册字段的 NVS key，使其重新使用当前固件声明的默认值；同 namespace 的未注册 key、WiFi、Web Auth、基础库设置和 LittleFS 均保留。恢复同样执行字段级和页面级校验、触发成功字段的回调并保留待重启提示；多字段恢复和普通保存一样不是跨 key 事务，失败时会明确报告部分完成。关键强一致配置应使用单个 POD/blob 或业务自定义提交模型。注册传入的字符串和 enum option 数组需保持固件生命周期有效。

Web 页面可优先使用 Esp32Base 的 UI baseline、helper 和页面能力；不要在业务项目里为样式问题临时复制 CSS 或绕开基础库壳层。单字段轻量编辑可使用行内编辑 helper，小型 1-3 字段表单可使用弹层表单 helper；二者会在支持 `fetch` 的浏览器中局部提交和局部替换，禁用 JS 时仍回退到普通表单提交。需要业务自控打开/关闭时，也可直接使用原生 `<dialog>` 并复用基础表单和按钮样式。现有页面能力不适合时，业务侧可以做最小局部实现；多个项目确实会复用的模式，再回到 Esp32Base 补通用 helper、样式、示例和文档。

需要真实时间时，业务项目应优先通过 `Esp32BaseTime::snapshot()` 获取统一时间快照。NTP 是最高优先级时间源；启用 RTC 时，DS3231 或 PCF8563 可在离线启动时提供真实时间。断网启动时业务仍能记录当前 `bootId + uptimeSec`；本次 boot 后续由 RTC 或 NTP 建立可信时间后，可用 `Esp32BaseTime::resolveCurrentBootEvent()` 只回填同一 boot 的相对时间事件，历史未知时间不会被伪造为日期。旧的 `Esp32BaseNtp::snapshot()` 仍表示 NTP 自身状态，不作为 RTC-only 设备的业务时间入口。

外部 RTC 通过 `ESP32BASE_ENABLE_RTC=1` 显式启用，当前支持 `ESP32BASE_RTC_DRIVER_DS3231` 和 `ESP32BASE_RTC_DRIVER_PCF8563`。同一个应用固件只选择一个驱动，不做运行时自动识别；硬件板确定后在 `platformio.ini` 中设置 `ESP32BASE_RTC_DRIVER`。默认 I2C 地址可用 `ESP32BASE_RTC_I2C_ADDR=0` 交给驱动选择：DS3231 为 `0x68`，PCF8563 为 `0x51`。基础库默认不调用 `Wire.begin()`，推荐业务在 `Esp32Base::begin()` 前初始化自己的 I2C 总线并调用 `Esp32BaseRtc::configure(Wire)`；如果希望基础库初始化 RTC I2C，可设置 `ESP32BASE_RTC_AUTO_WIRE_BEGIN=1`，并按需配置 `ESP32BASE_RTC_SDA`、`ESP32BASE_RTC_SCL` 和 `ESP32BASE_RTC_I2C_CLOCK_HZ`。RTC 芯片寄存器按 UTC 日历字段存储；显示和日志按 `ESP32BASE_NTP_GMT_OFFSET_SEC` / `ESP32BASE_NTP_DAYLIGHT_OFFSET_SEC` 固定偏移格式化，默认 UTC+8。RTC 中断、闹钟、方波、温度等芯片扩展能力不由基础库占用，业务项目可在同一 I2C 总线上自行访问；基础库只做低频读写时间、状态展示和 NTP 成功后的可选写回。示例见 `examples/rtc_time_source`。

RS485 半双工基础串口通过 `ESP32BASE_ENABLE_RS485_PORT=1` 显式启用。`Esp32BaseRs485Port` 只封装 ESP32 `HardwareSerial`、RX/TX/DE 引脚、baud、串口配置、发送前后 DE 方向切换、`flush()` 等待和轮询读取；它不包含 Modbus/RTU、CRC、地址、重试、超时帧解析或任何应用协议。业务协议应在应用层基于 `writeBytes()`、`readable()` 和 `readByte()` 自行实现。示例见 `examples/rs485_port`。

标准 MQTT 3.1.1 Client 在 `IOT` Profile 中默认启用，其他 Profile 也可通过 `ESP32BASE_ENABLE_MQTT=1` 显式开启。它基于 Arduino ESP32 Core 内置 ESP-MQTT，提供单 Broker、MQTTS、QoS 0/1、retain、LWT、每次 CONNECT 前更新应用借用的连接周期数据、固定容量订阅、重连重新订阅、退避抖动、分片消息安全组装和结构化诊断。默认必须提供 Broker CA，TLS 在 NTP 成功前保持 `WAITING_FOR_TIME`；RTC 可供离线业务记时，但不能单独放行公网 TLS。明文 MQTT 需要额外设置 `ESP32BASE_MQTT_ALLOW_PLAINTEXT=1` 并在运行配置中选择 `EXPLICIT_PLAINTEXT`。用户名、密码、证书、私钥、LWT 和订阅字符串由应用持有到设备重启，不写入 App Config、NVS、Web 或日志。示例见 `examples/mqtt_tls`。

MQTT 只负责连接机制。Topic 版本、命令授权、去重、过期、JSON、业务状态同步、离线业务数据和重连后的当前状态重发仍由应用负责。基础库没有第二套离线发送队列；`publish()` 成功只表示报文已被非阻塞发送队列接受，QoS 1 必须等待 `EVENT_PUBLISH_ACKNOWLEDGED` 才表示 Broker ACK，断线前未 ACK 的报文会报告“送达状态不确定”且可能由底层有界 outbox 重传；QoS 0 不提供无法证明的送达承诺。

Espressif 的 mbedTLS 配置只有在 `CONFIG_MBEDTLS_HAVE_TIME_DATE=y` 时才校验证书 `notBefore/notAfter`。本仓库实测支持的官方预编译 Arduino Core 2.0.16 和 3.3.8 均未启用它：CA 链和 hostname 仍会校验，但过期或尚未生效的证书不会仅因日期被拒绝。Esp32Base 默认拒绝在这种构建中配置 MQTTS，并报告 `ERROR_TLS_CERTIFICATE_DATE_CHECK_UNAVAILABLE`；如果产品评估后仍接受该上游限制，必须显式设置 `ESP32BASE_MQTT_ALLOW_UNCHECKED_CERTIFICATE_DATES=1`。`Status::certificateDateCheckEnabled` 暴露实际能力，不能把 NTP 就绪误解为底层已经执行日期校验。

WiFi 默认关闭 modem sleep，让 Web 首屏和 OTA 不被 Arduino ESP32 默认 `WIFI_PS_MIN_MODEM` 的 DTIM 唤醒抖动拖慢；电池设备可调用 `Esp32BaseWiFi::setPowerSave(true)` 恢复 modem sleep。

## 支持目标

芯片：

- ESP32
- ESP32-S3
- ESP32-C3

框架兼容目标：

- Arduino ESP32 Core 2.0.14+
- Arduino ESP32 Core 3.0.4+

当前发布矩阵实际验证`platformio/espressif32@6.7.0` + Core 2.0.16，以及`pioarduino 55.03.38-1` + Core 3.3.8；其它范围内minor版本需要业务项目按目标板重新构建和验证。仓库将两代PlatformIO包分别固定在`.piohome/arduino2`和`.piohome/arduino3`，发布验证不使用默认`~/.platformio`：

```sh
python3 scripts/ensure_arduino_platformio.py
python3 scripts/pio_arduino.py 2 run -d examples/basic -e esp32_local
python3 scripts/pio_arduino.py 3 run -d examples/basic -e esp32_local_arduino3
```

不支持：

- ESP8266
- 跨芯片 HAL
- HTTP Client 封装
- WebSocket
- HTTPS
- 多用户权限
- 大型前端 SPA
- 云平台绑定

## Profiles

默认 Profile 是 `ESP32BASE_PROFILE_MINIMAL`。

| Profile | 默认能力 | 用途 |
| --- | --- | --- |
| `ESP32BASE_PROFILE_MINIMAL` | Log、Config、System | 极简程序和专项能力组合 |
| `ESP32BASE_PROFILE_OFFLINE` | MINIMAL、Watchdog、Health、FS、FileLog | 无网络的可靠本地控制器 |
| `ESP32BASE_PROFILE_LOCAL` | OFFLINE、WiFi、配网、NTP、mDNS、认证Web、Web OTA | 可本地维护和升级的普通设备 |
| `ESP32BASE_PROFILE_IOT` | LOCAL、MQTT/MQTTS | 本地维护面加公网MQTT的智能终端 |

业务项目通常只需在 `LOCAL` 和 `IOT` 中选择。Bus、Sleep、RTC、RS485、Record Store、Conditions和App Config保持正交并按实际需求显式开启；特殊组合继续使用 `ESP32BASE_ENABLE_*` 精细覆盖，不新增排列组合Profile。完整契约见 [Profile 与裁剪](docs/02_profiles.md)。

仓库示例默认面向 ESP32 4MB Flash，并使用 `partitions/esp32-4mb-ota-balanced.csv`。应用项目强烈推荐直接选择 `partitions/` 中已有分区表，不要在业务项目里重新设计分区布局；除非硬件容量、OTA 策略或持久化容量确实不匹配，才自定义分区表，并必须同步验证串口烧录、OTA、NVS、LittleFS 和数据保留边界。ESP32 / ESP32-C3 4MB 推荐分区表保留两个 1.5MB OTA app slot，剩余空间作为 LittleFS 数据分区。

推荐分区表：

| 文件 | 适用场景 | NVS | OTA state | 单 app slot（最大固件） | LittleFS（最大文件数据） | coredump | 关键要求 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `partitions/esp32-4mb-ota-balanced.csv` | classic ESP32 4MB 默认双 OTA，两个 1.5MB 固件槽，剩余空间用于 LittleFS | `20 KB / 0x5000` | `8 KB / 0x2000` | `1.50 MB / 0x180000` | `896 KB / 0xE0000` | `64 KB / 0x10000` | `app0=0x10000`，兼容 PlatformIO/Arduino 默认上传偏移 |
| `partitions/esp32-c3-4mb-ota-balanced.csv` | ESP32-C3 4MB 双 OTA，两个 1.5MB 固件槽，剩余空间用于 LittleFS | `20 KB / 0x5000` | `8 KB / 0x2000` | `1.50 MB / 0x180000` | `896 KB / 0xE0000` | `64 KB / 0x10000` | C3 项目优先使用 |
| `partitions/esp32-s3-8mb-ota-balanced.csv` | ESP32-S3 8MB 双 OTA，较宽松 app/FS 空间 | `20 KB / 0x5000` | `8 KB / 0x2000` | `2.25 MB / 0x240000` | `3.38 MB / 0x360000` | `64 KB / 0x10000` | S3 8MB 项目优先使用 |

`Esp32BaseFs` 对业务暴露文本、二进制、追加、定长文件、目录、容量和按偏移读写 API，业务不需要直接 include `LittleFS.h` 或 Arduino `File`。容量读取优先使用 `storageInfo(total, used)`；固定容量文件先用 `createFixedFile()`，再用 `writeBytesAt()` 覆盖槽位；`appendBytes()` 适合低频追加。LittleFS mount failed 时不会自动格式化，需要清空或重建时只能通过明确维护动作调用 `Esp32BaseFs::format()` 或 Web System 页格式化入口。Web 格式化只清 LittleFS，不清 WiFi、Web Auth、业务 namespace 或 NVS 配置；业务如需同步清理，应使用 after-format callback 或事件自行处理。`/esp32base/fs?manage=1` 提供受限上传，上传保留本地文件名，只能选择已有目录；覆盖上传应避免中断时先清空旧文件。

LittleFS 中的 `/esp32base/**` 是基础库受管命名空间。系统诊断日志默认在 `/esp32base/logs/system.log`，已登记RecordStore位于 `/esp32base/records/**`；Web文件管理不允许上传、覆盖或删除受管路径。其他业务文件应放到 `/app/**`、`/data/**` 或项目自定义目录，并受 `Esp32BaseStorage::unmanagedWritableBytes()` 的容量保留约束。

`LOCAL` 和 `IOT` 统一使用HTTP Web OTA，不提供ArduinoOTA/espota或独立3232监听端口。浏览器使用multipart上传；命令行使用Esp32Base内置的高速 `webota` target。它复用现有 HTTP Web OTA 接口和 Web Auth。`custom_esp32base_webota_host` 同时接受设备 IP、普通 DNS hostname 和 mDNS `<hostname>.local`；DHCP 地址可能变化的本地开发设备推荐使用当前 Esp32Base hostname 对应的 `.local` 地址：

```ini
extra_scripts =
  post:path/to/Esp32Base/scripts/esp32base_webota.py

custom_esp32base_webota_host = esp32-demo.local
custom_esp32base_webota_user = <current-web-auth-user>
custom_esp32base_webota_password = <current-web-auth-password>
```

```sh
pio run -t webota
```

`webota` 是 Esp32Base 提供的 HTTP 上传方式，每次执行时由操作系统解析一次配置的 IP 或 hostname，显示本次解析到的地址，并让预检、状态采样和上传连接复用该地址；HTTP `Host` 和 HTTPS SNI 仍保留原 hostname。`.local` 依赖运行脚本的系统和当前网络支持 mDNS。它默认使用 `/esp32base/ota/raw` raw binary 路径和 64KB 分块，并在上传前预检认证、RSSI 和 OTA 目标分区容量，超出时不发送固件 body。raw 上传会根据弱 RSSI 自动把发送分块和节奏降到更保守的默认值，也可通过 `esp32base_webota_chunk_size`、`esp32base_webota_raw_pause_ms` 和 `esp32base_webota_socket_send_buffer` 显式覆盖；raw 传输中断时不会自动改走 `/esp32base/ota` multipart 路径。脚本会为 raw HTTP body 增加传输 padding，设备端只写入 `X-Firmware-Size` 声明的真实固件字节，以避免 Arduino `HTTPRaw` 最后一包短读等待超时。设备端把 raw 接收块直接交给 Arduino `Update` 内部缓冲，不再额外申请连续 64 KB heap。浏览器手工选择文件上传仍使用 `/esp32base/ota` multipart 表单路径，页面会在选择文件后立即显示固件大小，并在发送前按当前 OTA 目标 slot 容量拦截过大的固件，不受脚本默认值影响。详细配置见 `docs/05_ota.md`。

量产项目建议启用 `ESP32BASE_OTA_REQUIRE_MARK_VALID=1`，避免 Arduino core 在应用自检前过早把新 OTA 镜像标记为 valid。业务应把基础库启动、配置/API 注册、存储、传感器、执行器安全状态和核心任务纳入自检，通过后再调用 `Esp32BaseOta::markCurrentValid()`。确认期限在 Arduino `setup()` 前启动，不依赖业务进入 `Esp32Base::begin()`、loop 或持续调用 `Esp32Base::handle()`；期限届满会标记新镜像无效并重启回滚，`handle()` 只保留降级检查。最小配置和示例见 `docs/05_ota.md`。

IOT 产品如果协议要求正常离线消息，可在 `Esp32Base::begin()` 前注册 `Esp32Base::setBeforeNetworkStopCallback()`，在 Web OTA 上传、统一 restart 或 deep sleep 异步断开 MQTT 前执行一次固定时间、非阻塞的最后 publish。callback 可返回不超过 1000 ms 的有界网络发送宽限；基础库会在 restart/deep sleep 的异步断开前后应用该宽限，Web OTA 只在暂停 MQTT 前应用一次。它不会轮询 PUBACK，enqueue accepted 也不保证 Broker 已收到；异常掉电和正常离线消息未送达仍必须由 LWT 兜底。完整边界见 `docs/03_api.md`。

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
13. [Record Store 设计、接入与实机基准](docs/12_record_store.md)
14. [应用接入与版本适配](docs/13_integration_and_upgrade.md)
