# Esp32Base 能力变更记录

本文从 2026-05-06 起记录 Esp32Base 新增和优化的能力，面向正在接入本库的业务项目。业务项目应优先查看本文，了解最近可用的新 API、行为变化和推荐接入方式。

## 2026-05-13

### 冻结前诊断资产与 OTA/Web 边界收紧

调整：

- `factoryReset()` / `clearSystemConfig()` 不再清空整个 `eb_sys` namespace，只清理 `eb_sys.hostname`，保留 boot/restart/watchdog 统计和诊断 key。
- Watchdog trip 当 `wdt_trip_base` 大于 lifetime reset count 时显示 `invalid baseline`，不再把异常基线折算成 `0`。
- Reset Watchdog Trip 写入 `wdt_trip_base` / `wdt_trip_time` 后会立即回读确认，失败时返回错误提示。
- Web OTA 在 `startUpload()` 阶段失败时记录拒绝原因，后续 chunk 不再继续写入，最终 JSON 返回明确错误。
- Status 页将 `OTA headroom` 改名为 `OTA slot minus current sketch`；`Max OTA upload` 才是上传硬上限。
- 文档资源表同步刷新 ESP32 Core 2.x 7 profile、ESP32-C3 FULL 和 ESP32 Core 3.x FULL 的当前 size；历史条目中的 `OTA headroom` 指的就是改名前的同一状态页指标。
- Web 路由文档补齐 `/esp32base/tools/filelog` 和 `/esp32base/tools/logs-clear`，并标明 `/esp32base/logs/clear` 是保留直达入口。

关键边界：

- 出厂重置保护基础库诊断资产，但仍会清理 WiFi、Web Auth、FileLog 模式和 hostname 配置。
- `/esp32base/api/wifi` 仍是表单提交兼容端点，返回 303 到 HTML 页面，不是 JSON API。
- Core 3.x、实机 OTA/WDT/WiFi/AP/LittleFS/soak 仍需按发布清单继续验证。

## 2026-05-11

### FileLog 运行时模式收口

破坏性调整：

- `Esp32BaseFileLog` 改为模式 API：`setMode(OFF/WARN/INFO)`、`mode()`、`modeName()`；不再公开按 path/max/level/files 拼装启用的接口。
- 文件日志运行时只支持 OFF、WARN、INFO；DEBUG/VERBOSE 不作为文件日志模式，也不会出现在内置 Web 配置中。
- 文件日志默认值改为 `ESP32BASE_EB_FILELOG_DEFAULT_MODE`，可取 `ESP32BASE_FILELOG_MODE_OFF`、`ESP32BASE_FILELOG_MODE_WARN`、`ESP32BASE_FILELOG_MODE_INFO`。
- FileLog 持久化配置收口为 `eb_log.mode`；不读取旧的启用/等级 key，不做 NVS 迁移。
- Web System 页新增 FileLog 模式设置，保存后立即生效；Logs 页面继续只负责查看日志。
- Logs 页面在 FileLog OFF 时仍展示已有历史日志；OFF 只停止后续写入，不隐藏或删除历史内容。
- FileLog 模式变更 WARN 审计日志补充上一次模式和新模式，便于现场确认配置变化来源。
- 示例通过 `ESP32BASE_EB_FILELOG_DEFAULT_MODE=ESP32BASE_FILELOG_MODE_INFO` 设置默认 INFO 模式，不再在应用代码里重新配置 FileLog。

关键边界：

- `ESP32BASE_LOG_LEVEL` 仍是编译期最高保留日志等级；FileLog 默认模式和 Web 可选项都不能突破该上限。
- FileLog path、max bytes、rotate files、buffer、flush interval 继续是构建期资源策略，不在内置 Web 普通运维界面暴露。

### 内置 Web 导航与底部状态摘要

优化：

- Status 页重构为只读设备体检页，按 Overview、Hardware、Firmware & OTA、Network、Storage & Logs、Partition Table、Boot Reasons 分组展示。
- Status 页新增芯片型号、revision、CPU、SDK、eFuse MAC、STA MAC、AP MAC、heap max alloc、Watchdog lifetime/trip resets、OTA headroom 和完整运行时分区表。
- Web System 页新增 Watchdog trip 小计重置；只写入 `eb_sys.wdt_trip_base` 和 `eb_sys.wdt_trip_time`，保留 `eb_sys.wdt_cnt` 总累计；无可用时间时页面明确显示 `unknown (time unavailable)`。
- Watchdog trip reset 时间保存逻辑与 Status 页 Time 行使用同一可信 epoch 判断，避免页面显示 Time synced 但 trip reset time 仍写入 unknown。
- Status 页移除常驻 `OTA status` 行，保留 OTA 内部状态机和 `/esp32base/api/ota` 诊断字段；异常时仍显示 `Last OTA error`。
- eFuse MAC 按常见网络 MAC 顺序展示，便于和 STA MAC、AP MAC 直接对照调试。
- 修正 config portal 默认 AP SSID 后缀，`ESP32-Config-XXXX` 的 `XXXX` 现在取可读 eFuse MAC 的最后两个字节，而不是 `ESP.getEfuseMac()` 整数低 16 位。
- WiFi 配置支持开放 WiFi：SSID 仍必须非空，密码可为空；`clearCredentials()` 现在返回 NVS 清理真实结果，Web 清除失败会显示失败反馈。
- 分区表展示 Name、Type、SubType、Offset、Size、Role；Role 标识 running app、next OTA、app data、NVS config、OTA state、coredump 等调试语义。
- 默认系统导航改为底部紧凑区，顶部导航主要用于业务首页和业务页面。
- 默认系统入口收敛为 Status、Logs、System；WiFi、Auth、OTA 作为低频配置/维护入口收进 System 页，但保留原直达 URL。
- System 页入口使用见名知义的短名称：WiFi Setup、Web Auth、Firmware OTA。
- `BUILTIN_TOOLS` 默认标签从 `Tools` 改为 `System`，更准确表达设备级维护页面职责。
- 底部状态摘要保留 `Free heap:`，并新增简写 `Up:` 和 `RSSI:`，使用更小字号显示，便于快速判断运行时长和 WiFi 信号。
- 内置 Web 默认正文调整为 14px，标题为 18px/16px/15px，按钮、输入框、footer 和日志区域同步收紧为设备控制台尺度。

关键边界：

- 不新增主题系统或配置 API；业务仍可通过 `setHeadExtraCallback()` 覆盖样式。
- WiFi、Auth、OTA 页面职责不合并进 System，只在 System 页提供入口，避免低频配置表单和危险操作混在一个长页面中。

### Hostname 编译期默认值与持久化管理

新增/调整：

- 移除 `Esp32Base::setHostname()` 公开 API，不再支持应用代码运行期设置 hostname，避免 mDNS、ArduinoOTA 和 Web 展示出现半生效状态。
- 新增 `ESP32BASE_DEFAULT_HOSTNAME` 编译期默认 hostname，未配置时为 `esp32base`。
- 新增 `Esp32Base::defaultHostname()` 和 `Esp32Base::isValidHostname()`。
- `Esp32Base::begin()` 在 Config 初始化后读取 hostname：先使用 `ESP32BASE_DEFAULT_HOSTNAME`，再用合法的 `eb_sys.hostname` 覆盖。
- hostname 校验规则固定为 1-32 位小写字母、数字和短横线，不能以短横线开头或结尾，不能包含 `.` 或 `.local`。
- Web System 页新增 Hostname 设置区，显示当前值、默认值、已保存值和是否需要重启。
- 新增 `GET /esp32base/api/hostname` 与 `POST /esp32base/api/hostname`。
- Web/API 保存 hostname 只写入 `eb_sys.hostname`，不热切换当前运行时 hostname；重启后 mDNS、ArduinoOTA、状态页和 API 完整使用新名称。
- `factoryReset()` 清理 `eb_sys.hostname` 后，重启恢复 `ESP32BASE_DEFAULT_HOSTNAME`。

推荐接入：

```ini
build_flags =
  -D ESP32BASE_DEFAULT_HOSTNAME=\"esp32-fan\"
```

业务侧用途：

- 多台同类设备运行同一业务固件时，可先用构建默认 hostname 提供稳定出厂身份，再通过 Web/API 为每台设备保存唯一 hostname。
- 业务项目不需要自行保存/恢复 hostname，也不需要分别处理 mDNS、OTA 和 Web 展示。

关键边界：

- Hostname 是启动期设备身份，不支持运行期热切换。
- Web/API 修改成功后必须重启，才会影响 mDNS `<hostname>.local` 和 ArduinoOTA/espota。
- 出厂重置不会删除业务 namespace；只清除基础库 namespace，其中 `eb_sys.hostname` 会被清除。

### 日志、出厂重置与 Config 审计收口

新增/调整：

- `Esp32BaseLog` 新增 `setSerialLevel()` / `serialLevel()`，Serial 输出等级和 FileLog 写入等级解耦。
- `runtimeLevel()` 仍控制日志是否生成并分发给 sink/FileLog；`serialLevel()` 只控制 `Serial.println()`。
- 量产设备可设置 `Esp32BaseLog::setSerialLevel(Esp32BaseLog::NONE)` 关闭串口，同时保持 FileLog WARN/ERROR 或 INFO 正常写入。
- 新增 `Esp32BaseConfig::factoryReset()`。
- 新增单项清理 API：`clearWifiConfig()`、`clearWebAuthConfig()`、`clearSystemConfig()`、`clearLogConfig()`。
- `clearLibraryNamespaces()` 现在等价于 `factoryReset()`。
- namespace 不存在时清理返回成功，不创建空 namespace，也避免底层 `NOT_FOUND` 噪声。
- 文档明确 `enableConfigAudit(true)` / `enableConfigReadAudit(true)` 可在 `Esp32Base::begin()` 前调用；需要覆盖基础库初始化读写时必须在 begin 前开启。

推荐接入：

```cpp
Esp32BaseLog::setSerialLevel(Esp32BaseLog::NONE);
Esp32BaseFileLog::setMode(Esp32BaseFileLog::WARN);
```

```cpp
Esp32BaseConfig::factoryReset();
Esp32BaseConfig::flushAll();
Esp32BaseSystem::restart("factory reset");
```

关键边界：

- 如果 `ESP32BASE_LOG_LEVEL=ESP32BASE_LOG_NONE`，日志宏会被编译掉，FileLog 也收不到日志。
- `factoryReset()` 不自动重启、不格式化 LittleFS、不删除 FileLog 日志文件内容、不清理业务 namespace。
- `enableConfigReadAudit(true)` 噪声较多，读取审计主要在 DEBUG 编译等级下观察。

## 2026-05-10

### Config 小型结构化元数据

新增：

- `Esp32BaseConfig` 新增 `setBlob()` / `getBlob()` / `setBlobDeferred()`，用于一次保存和读取最大 256 字节的小型固定大小元数据。
- 新增 `setPod<T>()` / `getPod<T>()` / `setPodDeferred<T>()` 模板薄封装，要求类型为 trivially copyable 且 standard-layout，适合保存 `head/count/next_id` 这类环形文件元数据。
- blob 同步写入会做写前比较；旧值长度和内容完全相同时返回 true 并跳过 NVS 写入。
- blob deferred 写入支持 pending read-through、同 key 合并、相同 pending 跳过，以及 `flushAll()` 强制落盘。

业务侧用途：

- 业务项目的 RecordStore/EventStore 可把 `head`、`count`、`next_id` 合并为一个 POD metadata key，避免每条记录或事件 append 后分别写 3 个 NVS key。
- blob API 只保存小型结构化元数据；记录正文、事件环形文件和高频日志仍应由业务文件或 FS 能力承载。

文档：

- `docs/03_api.md` 补充 blob/POD API、256 字节上限、namespace/key 校验、失败返回、相同值跳过写入、deferred/flushAll 语义和示例。
- `full_demo` 示例增加 POD metadata 的 deferred 写入和 pending read-through 示例。

## 2026-05-09

### Web POST 轻量同源防护与 OTA 健壮性

修复：

- 内置危险 POST 增加轻量 `Origin` / `Referer` host 校验；存在这些头时必须与请求 `Host` 同源，缺失时继续放行，保证 curl、PlatformIO `webota` 和简单脚本可用。
- 覆盖 WiFi 保存/清除、Auth 保存、重启、Tools 操作、Logs clear、Web OTA upload/done；校验失败返回 403，不执行副作用。
- Web OTA 开始上传前检查下一 OTA 分区容量；固件超过 app slot 时给出明确错误，不进入写入流程。
- OTA 写入过程检查累计写入不能超过声明总大小；客户端伪造过小 size 后继续写入时会 abort。
- 设备端 OTA 日志增加上传摘要，成功、失败或 abort 时输出耗时、已上传容量和平均速度，例如 `upload_summary duration=13.49s uploaded=1020.2 KB average=75.7 KB/s`。
- `Update.begin()` 失败时记录底层 Update 错误字符串，减少只看到通用失败信息的调试成本。
- NTP 同步判定同时检查 SNTP 同步状态和最小 epoch，避免设备曾设置过 RTC 时误判已完成网络对时。
- `scripts/esp32base_webota.py` 新增 `esp32base_webota_upload_timeout`，默认 600 秒；原 `esp32base_webota_timeout` 继续用于预检和普通请求。
- `full_demo`、`web_logs_ota` 和 `net_runtime` 示例补充 ESP32-S3 8MB env，避免 S3/8MB 板使用 ESP32 4MB 分区表浪费空间。

文档：

- 内置 Web 导航标签枚举移除旧 Reboot 历史别名；系统工具页统一使用 `BUILTIN_TOOLS`，并移除旧 `/esp32base/reboot` 路由。
- 移除顶层 `Esp32BaseLog.h` / `Esp32BaseConfig.h` 转发头，公开入口统一为 `#include <Esp32Base.h>`。
- `webota` 配置只读取 env 中的 `custom_esp32base_webota_*`、`[esp32base_webota]` 公共段和环境变量，不再兼容非 `custom_` env option。
- 明确多核/多任务项目推荐单系统服务任务模型：`Esp32Base::begin()`、`Esp32Base::handle()`、`Esp32BaseConfig`、Web、Bus 固定在同一个 loop/system task 中调用。
- 明确 `Esp32BaseConfig` 当前不是线程安全 API；业务 task 应通过 FreeRTOS queue/flag/ring buffer 投递配置变更，再由 loop/system task 调用 `setXxx()` 或 `setXxxDeferred()`。
- 明确 `setAuthEnabled(false)` 会完全开放内置 HTTP 路由，包括 OTA、重启、Tools 和配置提交，只适合受控调试网络。
- 补充 `Esp32BaseWeb::redirectSeeOther()` API、`Esp32BaseFileLog` 最小示例，以及示例默认 ESP32 4MB 分区、S3/C3/8MB 板型需选择匹配 env/分区表。

设计边界：

- 本轮不改变个人项目/调试优先安全模型：INFO 明文日志、默认弱口令、开放配置 AP、OTA 无签名、NVS 明文、可关闭 Web Auth 仍按既定设计保留。
- 本轮不给 `Esp32BaseConfig` 加 mutex，也不引入完整 CSRF token 系统；多任务安全通过明确单任务调用模型和业务侧队列投递实现。

## 2026-05-08

### 启动严格模式与 Captive DNS 生命周期修复

修复：

- `ESP32BASE_STRICT_OPTIONAL_BEGIN=1` 现在会对 FS、FileLog、Watchdog、Sleep、Health、WiFi 等所有可选模块生效；任一可选模块 begin 失败都会使 `Esp32Base::begin()` 返回 false。
- Captive Portal DNS 只在 WiFi 处于 `CONFIG_PORTAL` 时运行；设备切回 STA 或离开配网页后会停止 DNS server，避免旧的 captive DNS 状态残留。
- `full_demo` 自测用例同步当前 Tools 页面结构，避免启用自测时因过期 CSS 类名误报失败。

验证：

- 新增 `scripts/check_trim_symbols.py`，用于在 `examples/basic` 构建后检查最终 ELF 是否包含被禁用 profile 的重模块符号。

### 内置 Web 页面视觉统一

优化：

- 内置 Web 默认正文统一为 14px，标题层级统一为 18px / 16px / 15px。
- 页面最大宽度提升到 1040px，顶部导航、正文 panel 和底部系统入口使用一致的设备控制台布局。
- Status、WiFi、Auth、OTA、Logs、System 页面统一使用 `h1` 页面标题、白色 panel、简洁按钮、文本输入框和中性色配色。
- System 页面把标题、说明和危险操作按钮收进同一个 panel，避免标题和操作区域错位。
- 页面标题改为独立 header panel，与正文 panel 的内边距和视觉起点对齐。
- Logs 页面移除清空按钮，只负责查看日志；`Clear logs` 维护动作移到 System 页面。
- Logs 页面输出大日志时周期性 yield/feed Watchdog，降低长日志请求触发看门狗的风险。

关键边界：

- 只调整内置默认 HTML/CSS，不新增主题系统或配置 API。
- 业务项目仍可通过 `setHeadExtraCallback()` 注入 CSS 覆盖默认视觉。

### 快速命令行 Web OTA

新增：

- Esp32Base 提供可复用 PlatformIO extra_script，注册 `webota` target，可通过现有 HTTP Web OTA 接口上传固件。
- `webota` 使用 `/esp32base/ota`、Web Basic Auth、`X-Firmware-Size` 和默认自动 SHA256 校验。
- `webota` 默认 64 KB 分块上传，并在上传前通过状态接口预检连接和认证，减少反复烧录耗时和错误密码等待。
- `webota` 日志显示友好容量、开始/结束时间、用时、平均速度，并按约 5% 粒度输出进度。
- 支持 IP 地址上传，不依赖 mDNS。

业务侧用途：

- 业务项目无需复制脚本或自建临时 target；配置地址和认证后可直接运行：

```sh
pio run -t webota
```

推荐接入：

```ini
extra_scripts =
  post:path/to/Esp32Base/scripts/esp32base_webota.py

custom_esp32base_webota_host = 192.168.2.112
custom_esp32base_webota_user = admin
custom_esp32base_webota_password = admin
```

关键边界：

- `espota` 是 ArduinoOTA 标准协议，适合标准工具链兼容；`webota` 是 Esp32Base 提供的 HTTP 上传方式，通常更适合反复快速烧录。
- `webota` 复用现有 Web OTA 页面/API，不改变浏览器 Web OTA，也不影响 ArduinoOTA/espota。
- 失败会区分连接失败、认证失败、设备返回错误和上传中断。

### ArduinoOTA / espota 命令行 OTA

新增：

- 启用 `ESP32BASE_ENABLE_OTA` 时默认启用 `ESP32BASE_ENABLE_ARDUINO_OTA`，支持 PlatformIO `upload_protocol = espota`。
- ArduinoOTA 使用 `Esp32Base::hostname()`，可通过 `<hostname>.local` 和标准端口 3232 上传。
- espota 认证密码复用当前生效的 Web Auth 密码；即使关闭 Web Auth，espota 仍要求密码。
- Web OTA 与 espota 共用 OTA 状态和 Watchdog、NVS deferred flush、WiFi power save、FileLog 资源保护策略，避免同时写 flash。

业务侧用途：

- 业务项目无需再各自写 PlatformIO 脚本包装 `/esp32base/ota`，可直接使用 `pio run -t upload --upload-port <hostname>.local`。
- 不需要命令行 OTA 的项目可显式设置 `ESP32BASE_ENABLE_ARDUINO_OTA=0`，保留原 Web OTA。

关键边界：

- 非 OTA profile 不新增 ArduinoOTA 能力，不改变现有裁剪边界。
- espota 只有密码没有用户名，因此用户名不参与命令行 OTA 认证。
- Web OTA 继续支持可选 SHA256；ArduinoOTA/espota 使用协议内建 MD5。

推荐接入：

```ini
upload_protocol = espota
upload_port = esp32base-full.local
upload_flags =
  --auth=admin
```

## 2026-05-07

### 启动流程 DEBUG 诊断

优化：

- `DEBUG` 编译级别下，启动流程输出 `begin_start`、模块 `module_begin/module_ready`，以及 Web/NTP/mDNS/OTA 延迟启动或停止原因。
- FS mount 成功、Web route 注册准备、OTA ready 分区信息增加 DEBUG 诊断日志。
- 旧 Web Auth 存储缺少 `auth_pass` 时，启动时静默回退默认认证，不再触发 `Preferences.cpp ... auth_pass NOT_FOUND` 底层错误日志。
- `full_demo` 内置 OTA 导航标签统一显示为 `OTA`，不再显示 `Update`。
- `/esp32base/ota` 上传成功后，页面提示设备重启，并在短暂等待后跳转到当前配置的首页。
- `/esp32base/ota` 上传失败时留在 OTA 页面，显示失败原因，并允许重新上传。
- 新增 `Esp32BaseWeb::setHeadExtraCallback()`，业务项目可在 `sendHeader()` 输出顶部导航前注入 CSS。
- 顶部业务导航自动为当前匹配项输出 `active` class，嵌套路由按最长 path 前缀匹配。
- 基础库默认链接样式改为更中性，普通链接不再默认渲染成强蓝色按钮。
- Web 默认配色改为简洁中性色，导航和 tabs 的 active 状态使用浅绿灰背景和深绿灰文字，不再使用蓝色主色。
- 新增 `Esp32BaseFs::format()`；`full_demo` 在首次调试遇到 LittleFS 未格式化导致 FileLog 启用失败时，会格式化 FS 并重试启用 INFO 文件日志。
- 内置系统首页默认命名改为 `Status`；原 `Restart` 入口升级为 `Tools` 维护页，用于承载重启和格式化 LittleFS 等维护操作。

业务侧用途：

- 应用使用 DEBUG 串口日志时，可以直接确认启动顺序、模块初始化进度和条件启动原因。
- 旧测试设备升级后，缺少 `auth_pass` 的 NVS 记录不会污染启动日志。
- OTA 成功后浏览器会自动回到设备首页，便于确认新固件启动状态。
- OTA 失败后可直接看到失败原因并重试，不需要手动刷新页面。
- 业务项目可继续使用 `addPage()` / `addNavItem()` / `sendHeader()`，同时把业务 CSS 提前放进 head，避免页面刷新时导航先闪成基础库默认样式。
- 业务项目即使不注入 CSS，也会得到低侵入、设备控制台风格的默认导航。
- `full_demo` 首次烧录后即使 LittleFS 尚未初始化，也能自动恢复文件日志页面。
- `Tools` 页面提供显式维护动作；格式化 LittleFS 可用于调试期恢复未初始化或损坏的文件系统。

关键边界：

- 这些 DEBUG 日志不在默认 INFO 构建中输出。
- 文件日志模式独立于串口编译日志等级；`full_demo` 使用 INFO 文件日志便于观察，业务长期运行建议按需要保持 WARN。
- 不新增高频 loop、HTTP 请求或 NTP pending DEBUG 日志。
- `setHeadExtraCallback()` 回调只应输出 `<style>`、`<meta>` 等 head 内容，不应输出 body 内容。
- 默认样式只提供基础可读性，不新增主题系统或颜色配置 API。
- 基础库默认不会自动格式化 FS；`format()` 会清除 LittleFS 文件，仅由示例或应用在明确可接受数据丢失时调用。
- `Format LittleFS` 会删除日志和所有 LittleFS 文件，但不会清除 WiFi、Web Auth 或 NVS 配置；`Tools` 页面统一承载维护操作。
- Tools 页格式化成功后会明确提示，并重新加载 FileLog 模式；FileLog 会在格式化后重新创建日志目录，避免后续写入触发底层 VFS 目录不存在错误。
- 格式化 LittleFS 会输出 WARN 级维护日志，记录请求来源、format/mount/FileLog reload 结果；FileLog 目录或 segment 准备失败也会输出 WARN。
- FileLog 切换模式前会先 flush 现有 buffer，避免启动阶段 boot session 多行日志在示例或维护操作重新加载 FileLog 模式时丢失尾部。
- 普通 `GET` 页面慢请求日志降为 DEBUG；非 GET 操作慢请求仍保持 WARN，避免维护/写操作耗时被忽略。
- 配置审计中 `changed=no result=skipped`、读取审计和 deferred 排队日志降为 DEBUG；实际写入成功/flush 成功仍为 INFO，失败为 WARN/ERROR。
- 普通页面 Basic Auth 校验成功降为 DEBUG；认证缺失、无效或密码错误仍为 WARN，启动认证加载和认证修改继续保持 INFO。
- 业务页面/导航注册日志降为 DEBUG，减少示例 INFO 文件日志中的启动噪音。
- Tools 页面维护操作改为轻量分组布局，避免 `Restart device` 按钮和 `Format LittleFS` 标题贴近。
- `POST /esp32base/tools/reboot` 响应会把浏览器历史替换为 `/esp32base/tools?restarting=1`，避免刷新重复提交；重启请求输出 `restart_requested source=tools` WARN 日志。
- OTA 上传增加 INFO 生命周期日志：`upload_start`、10% 阶段 `upload_progress`、`upload_success`；进度带 `%`，字节数使用 KB/MB/B 友好展示，失败仍输出 ERROR。

推荐接入：

- 业务项目需要启动诊断时，编译开启 `ESP32BASE_LOG_LEVEL=ESP32BASE_LOG_DEBUG`；文件日志模式按设备用途单独选择，示例观察可用 INFO，长期运行建议 WARN。

### full_demo 日志等级调整

优化：

- `examples/full_demo` 编译日志等级改为 DEBUG，便于串口观察调试信息。
- `examples/full_demo` 文件日志默认模式保持 INFO，便于在 `/esp32base/logs` 观察示例运行和 Web/OTA 行为。

业务侧用途：

- 示例串口调试信息更完整，同时 `/esp32base/logs` 中可查看 INFO 级别的示例运行日志。

关键边界：

- 只调整 `full_demo` 示例；库默认日志等级和其他示例不变。

推荐接入：

- 业务项目如需类似观察体验，可编译开启 `ESP32BASE_LOG_LEVEL=ESP32BASE_LOG_DEBUG`，并按需要将 FileLog 设为 INFO；长期运行设备建议使用 WARN。

### 系统首页容量显示简化

优化：

- `/esp32base` 系统首页的 Heap、Flash、FS、File log 容量改为只显示 KB/MB/B 人性化格式，不再重复 raw bytes。

业务侧用途：

- 设备首页更易读，避免 `198280 bytes (193.63 KB)` 这类重复信息挤占页面宽度。

关键边界：

- 只调整内置系统首页展示；状态 API、OTA 进度等需要 raw bytes 的位置保持原有输出。

推荐接入：

- 业务项目无需改接入代码，更新 Esp32Base 后刷新 `/esp32base` 即可看到新展示。

### NTP 未同步日志降噪

优化：

- 移除基于 `isTimeSynced()` 状态查询产生的周期性 `ntp_sync_pending` WARN。
- NTP 未同步状态不再重复输出 WARN 或 DEBUG。

业务侧用途：

- 网络或 NTP 服务器暂时不可用时，应用日志不会被未同步状态刷屏。

关键边界：

- `ntp_client_started`、`time_synchronized`、`time_mapping` 和日志时间戳切换仍保持 INFO 输出。
- NTP 同步判断仍使用 `ESP32BASE_NTP_SYNC_MIN_EPOCH`。
- 只有接入明确的单次同步失败事件时，才应为该失败事件输出 WARN。

推荐接入：

- 业务项目无需配置；通过系统页或 API 查看当前 NTP 是否已同步。

### WiFi 与 Web Auth INFO 明文凭据日志

优化：

- WiFi 在 INFO 日志中输出 SSID 和明文密码，覆盖表单提交、凭据设置和 STA 连接。
- Web Auth 在 INFO 日志中输出认证用户名和明文密码，覆盖默认认证设置、保存认证和 Basic Auth 请求校验。
- Web Auth 启动加载认证时输出 `auth_loaded user=<user> password=<pass> source=stored|default`。
- Web Auth 明文密码持久化到 `eb_web.auth_pass`，认证校验直接比较明文密码。
- Basic Auth 请求日志改为每个 HTTP 请求最多输出 1 条，避免 OTA 上传分块时重复刷屏。

业务侧用途：

- 应用项目开发和现场调试时，可直接从日志确认 WiFi 凭据和 Web 认证凭据。

关键边界：

- Web Auth 持久化保存明文密码；缺少 `auth_pass` 的旧认证记录按无效存储处理并回退默认认证。
- WiFi 凭据按既有配置策略保存到 NVS。
- HTML、JSON 和 API 响应仍不输出 Web Auth 明文密码；WiFi 配置页仍按既有策略回显 WiFi 密码。

推荐接入：

- 业务项目无需额外开启 DEBUG；保持 INFO 日志即可看到 WiFi 和 Web Auth 明文凭据。

### Health 普通 tick 日志降频

优化：

- `DEBUG health tick loopMax=...` 默认从每 30 秒一次降为每 30 分钟最多一次。
- 新增 `ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS`，默认 1800000ms；设为 0 可关闭普通 DEBUG tick 日志。

业务侧用途：

- DEBUG health tick 不进入文件日志模式，不会污染现场 INFO 文件日志。

关键边界：

- `health.tick` bus 事件仍按 `ESP32BASE_HEALTH_TICK_INTERVAL_MS` 默认每 30 秒发布。
- `WARN health loop_slow...` 仍在 30 秒窗口超过 `ESP32BASE_HEALTH_LOOP_WARN_MS` 时立即输出。

推荐接入：

- 业务项目无需绕过 Health；默认配置即可减少日志噪声。需要完全静默普通 tick 时，在 build flags 中设置 `ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS=0`。

### Logs 页面 segment 标签大小显示

优化：

- Logs 页面顶部 `current-0`、`history-N` 标签中的文件大小改为只显示 KB/MB/B 人性化值，不再同时显示 raw bytes。
- Logs 页面文件日志状态信息改为 14px 小字号，`Buffer`、`Max per file`、`Max total`、`Segments` 中的字节数也只显示 KB/MB/B 人性化值。
- Logs 页面文件日志状态信息改为紧凑 label/value 表格；segment 标签中的文件名和大小拆成两个视觉字段，避免 `history-2 0 B` 被误读。
- Logs 页面文件日志状态信息放入普通 panel，和页面标题、日志内容区域保持一致的左侧留白。
- 内置页面默认正文为 14px，导航为 14px，页面标题收敛到 18px/16px/15px 层级，footer 为 13px，整体更接近设备维护控制台。

业务侧用途：

- 日志文件切换标签更简洁，避免 `9167 bytes (8.95 KB)` 这类重复信息挤占移动端宽度。

关键边界：

- 只调整 Logs 页面顶部 segment 标签的展示文本；状态页、API、OTA 进度等需要 raw bytes 的位置保持原有 raw + human 输出。

推荐接入：

- 业务项目无需改接入代码，更新 Esp32Base 后刷新 `/esp32base/logs` 即可看到新展示。

## 2026-05-06

### Fs 按偏移二进制读写

新增：

- `Esp32BaseFs::readBytesAt(const char* path, uint32_t offset, uint8_t* out, size_t maxLen, size_t* readLen)`
- `Esp32BaseFs::writeBytesAt(const char* path, uint32_t offset, const uint8_t* data, size_t len)`

业务侧用途：

- 可直接通过 Esp32Base Fs 实现二进制定长日志分页读取。
- 可直接通过 Esp32Base Fs 实现固定容量环形日志覆盖写入。
- 业务项目不需要 include `LittleFS.h` 或 Arduino `File`。

关键行为：

- `readBytesAt()` 支持从指定 offset 读取，读到 EOF 前允许短读，并通过 `readLen` 返回实际读取长度。
- `writeBytesAt()` 只覆盖已有文件内容，不创建文件、不扩展文件、不填洞。
- FS 未 ready、path 非绝对路径、文件不存在、offset 越界、写越界或底层读写失败时返回 `false`。

推荐接入：

- 业务先用 `writeBytes()` 或分块 `appendBytes()` 初始化固定容量文件。
- 后续按记录大小计算 offset，用 `readBytesAt()` 分页读取，用 `writeBytesAt()` 覆盖环形槽位。

### Web 当前请求方法 API

新增：

- `Esp32BaseWeb::METHOD_UNKNOWN`
- `Esp32BaseWeb::currentMethod()`
- `Esp32BaseWeb::isMethod(Esp32BaseWeb::Method method)`
- `Esp32BaseWeb::currentMethodName()`

业务侧用途：

- 可用 `METHOD_ANY` 注册同一路径，例如 `GET /api/faucet/config` 读取配置、`POST /api/faucet/config` 保存配置。
- handler 内可可靠区分 GET 和 POST。
- 业务项目不需要直接访问底层 `WebServer`。

关键行为：

- handler 上下文中，GET 返回 `METHOD_GET`，POST 返回 `METHOD_POST`。
- handler 外返回 `METHOD_UNKNOWN`。
- `isMethod(METHOD_ANY)` 在有效 handler 请求中返回 true。
- `METHOD_ANY` 仍用于路由注册，不作为实际请求方法返回。

推荐接入：

```cpp
void handleConfigApi() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_GET)) {
        Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
        return;
    }
    if (Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::redirectSeeOther("/config?saved=1");
        return;
    }
    Esp32BaseWeb::sendText(405, "method not allowed");
}

Esp32BaseWeb::addRoute("/api/faucet/config", Esp32BaseWeb::METHOD_ANY, handleConfigApi);
```

### Web 通用 chunked 响应与 CSV 导出

新增：

- `Esp32BaseWeb::beginResponse(int code, const char* contentType, const char* filename = nullptr)`
- `Esp32BaseWeb::beginText(int code)`
- `Esp32BaseWeb::beginCsv(int code, const char* filename = nullptr)`
- `Esp32BaseWeb::endResponse()`
- `Esp32BaseWeb::sendBytes(const uint8_t* data, size_t len)`
- `Esp32BaseWeb::writeCsvEscaped(const char* text)`

业务侧用途：

- 可在应用 handler 内输出 CSV、JSONL、文本、二进制或其他自定义 content type 的 chunked 响应。
- CSV 导出可通过 `beginCsv(200, "records.csv")` 自动发送 `Content-Type: text/csv; charset=utf-8` 和下载用 `Content-Disposition`。
- 业务项目不需要访问底层 `WebServer`，也不需要复制 Esp32BaseWeb 内部 chunked 实现。

关键行为：

- `beginResponse()`、`beginText()`、`beginCsv()` 只能在 handler 请求上下文中成功；handler 外返回 false 并记录 WARN。
- contentType 为空、超过 63 字节或包含控制字符时返回 false。
- filename 非空时只允许字母、数字、`.`、`_`、`-`，非法时返回 false。
- `sendChunk()` / `sendBytes()` 只能在已开始的 chunked 响应中输出；响应结束必须调用 `endResponse()`。

推荐接入：

```cpp
void handleRecordsCsv() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!Esp32BaseWeb::beginCsv(200, "records.csv")) {
        return;
    }
    Esp32BaseWeb::sendChunk("id,name\r\n");
    Esp32BaseWeb::sendChunk("1,");
    Esp32BaseWeb::writeCsvEscaped("zone-a");
    Esp32BaseWeb::sendChunk("\r\n");
    Esp32BaseWeb::endResponse();
}
```

### Web 业务优先导航与首页模型

新增：

- `Esp32BaseWeb::HomeMode`
- `Esp32BaseWeb::SystemNavMode`
- `Esp32BaseWeb::BuiltinPage`
- `Esp32BaseWeb::addNavItem(const char* path, const char* title)`
- `Esp32BaseWeb::setDeviceName(const char* name)`
- `Esp32BaseWeb::setHomePath(const char* path)`
- `Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HomeMode mode)`
- `Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SystemNavMode mode)`
- `Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BuiltinPage page, const char* label)`

业务侧用途：

- 可声明业务页面是设备主界面，基础库 WiFi、OTA、Logs、System 是系统工具。
- 可设置 `/` 和 `/esp32base` 使用基础库首页、业务首页优先或融合首页。
- 可把系统工具放在顶部、底部或底部紧凑系统工具区。
- 可覆盖 Home/WiFi/OTA/Logs/System/Auth 标签，适配中文业务项目。

关键行为：

- 未配置时保持 Esp32Base 默认首页和默认导航。
- `addPage()` 注册业务页面并进入业务导航；`addNavItem()` 只注册导航项，不注册路由。
- 配置业务首页后，导航品牌链接指向业务首页，业务主导航不会重复显示同一个首页入口。
- `HOME_APP` 和 `HOME_COMBINED` 下，设备根路径 `/` 都进入业务首页；`HOME_COMBINED` 仅保留 `/esp32base` 为融合首页。
- 内置页面和业务页面复用同一套 `sendHeader()` / `sendFooter()` 导航框架。
- `SYSTEM_NAV_SECTION` 使用底部小字系统入口，与 `Free heap`、`Up`、`RSSI` 同行展示；移动端允许系统入口自然换行，避免挤压和横向滚动。

推荐接入：

```cpp
Esp32BaseWeb::setDeviceName("Faucet");
Esp32BaseWeb::setHomePath("/status");
Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION);
Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_HOME, "状态");
Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_WIFI, "网络");
Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_LOGS, "日志");
Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_TOOLS, "工具");
Esp32BaseWeb::addPage("/status", "状态", handleStatusPage);
Esp32BaseWeb::addPage("/config", "配置", handleConfigPage);
```

### Web input style scope

优化：

- `sendHeader()` 输出的基础 CSS 不再使用过宽的 `input:not([type=submit]):not([type=button])` 选择器。
- 默认输入框样式仅作用于文本类 input：未声明 type 的 input、`text`、`password`、`number`、`email`、`url`、`tel`、`search`。
- `checkbox`、`radio`、`file`、`range`、`color`、`hidden` 等非文本控件保持浏览器原生尺寸和行为。

业务侧用途：

- 业务页面复用 `Esp32BaseWeb::sendHeader()` / `sendFooter()` 时，可以直接使用 checkbox/radio 表单项，不需要写高优先级 CSS 覆盖基础库样式。
- 内置页面未来新增非文本控件时，也不会被默认文本输入框样式污染。

推荐接入：

```html
<label><input type="checkbox" name="beep" value="1"> 蜂鸣器</label>
<label><input type="radio" name="mode" value="auto"> 自动</label>
```

### Runtime/Core robustness cleanup

优化：

- `Esp32BaseFs::listDir()` 遍历目录时显式关闭每个 `File` 句柄，并在非目录路径上关闭 root 句柄后返回 false，避免目录遍历积累底层文件句柄。
- `Esp32BaseNtp::isTimeSynced()` 不再使用旧的硬编码 `1600000000` 阈值，新增 `ESP32BASE_NTP_SYNC_MIN_EPOCH`，默认 `1700000000UL`。
- `Esp32BaseWatchdog::begin(timeoutMs)` 明确要求 `timeoutMs >= 1000`；更小值返回 false 并记录 WARN，避免 Arduino ESP32 2.x 下秒级参数被截断为 0。
- `Esp32BaseBus::publish()` 先快照本次匹配订阅，再调用 callback；callback 内允许 `unsubscribe()`，已取消的后续订阅不会继续收到当前事件，新订阅不会收到当前正在发布的事件。
- `Esp32BaseConfig` 字符串读取和写前比较不再使用 Arduino `String`，改用固定 scratch buffer 和 NVS 字符串读取，减少配置读取造成的 heap 碎片风险。
- `Esp32BaseSystem::restart()` 和 `Esp32BaseSleep::deepSleepUs()` 去除冗余的前置 `Esp32BaseFileLog::flush()`，保留记录 restart/sleep 日志后的最终 flush。
- `docs/01_architecture.md` 的 begin/handle 顺序补充 FileLog 位置，与当前代码一致。

业务侧影响：

- 目录列举、事件订阅变更、NTP 同步判断、Watchdog 参数和 Config 字符串读取的边界行为更明确。
- 应用如覆盖 NTP 同步判断，可在 include 前定义 `ESP32BASE_NTP_SYNC_MIN_EPOCH`。

### Web system page and log viewer diagnostics

优化：

- `/esp32base` 系统页不再只显示 `Ready`，改为展示固件、profile、hostname、uptime、boot count、reset/wake reason、heap、flash，以及当前 profile 可用的 WiFi/IP/RSSI、FS、FileLog、NTP、OTA 状态。
- Logs 页面改为单文件展示，默认显示最新 `current-0`，可通过顶部标签切换 `history-1`、`history-2` 等历史文件，避免多个日志文件都有内容时页面过大。
- Logs 页面保留所有 history/current segment 标签和大小，包括 0 字节文件，便于准确判断轮转文件状态。
- Logs 页面只做 HTML escape，不折叠、不省略、不规整空白行，页面展示忠实反映文件内容。
- Core Log 不再规整 message 中的 CR/LF；库内部需要单行日志时由调用点传入单行 message。
- 新增 boot session 启动诊断块，FileLog 初始化后输出 `boot` tag 多行短日志，包含 `boot_count`、reset/wake reason 及中文说明、固件、版本、build、profile、hostname、heap、flash，避免单行过长被日志缓冲截断。
- 新增 `Esp32BaseSystem::bootCount()`，返回 `uint32_t` 启动会话计数；NVS 使用 unsigned key `eb_sys.boot_cnt` 存储，每次固件启动加 1。
- 新增 `Esp32BaseSystem::resetReasonText()` / `wakeReasonText()`，Web 系统页和 `/esp32base/api/status` 同时输出 reason 原始值与中文说明。
- 新增 Web Basic Auth 配置管理能力：`setDefaultAuth()`、`authUser()`、`verifyAuth(user, pass)`、`saveAuth()`、`resetAuth()`。
- Web Auth 认证优先级明确为：已保存认证 > 应用默认认证 > 库默认 `admin/admin`；`setDefaultAuth()` 不覆盖用户已保存认证。
- 新增内置 `/esp32base/auth` 认证管理页面，并加入系统工具导航；`BUILTIN_AUTH` 可用于本地化标签，中文推荐“认证”。
- Web Auth 可持久化到 `eb_web.auth_user`、`eb_web.auth_pass`，明文密码用于启动日志、认证校验和现场调试。
- Web Auth 更新后立即生效；浏览器缓存旧 Basic Auth 时，后续请求会重新触发认证。
- OTA 与 Auth 配置解耦：OTA route 按 profile/OTA 编译条件注册；Web Auth 开启时受 Basic Auth 保护，Web Auth 关闭时无密码保护。
- `clearLibraryNamespaces()` 会清理 `eb_web`，恢复出厂时同步清除持久化 Web Auth。

业务侧用途：

- 业务项目访问 `/esp32base` 可以直接看到设备基础状态，不需要另写基础诊断页。
- 现场查看日志时，日志页更容易判断轮转文件顺序、空 segment 和异常空白来源。
- 业务项目可用 `bootCount()` 判断固件累计启动会话次数，并结合 `resetReason()` / `wakeReason()` 判断本次启动来源。
- 业务项目可直接链接 `/esp32base/auth`，让用户修改设备认证账号/密码，不需要自行保存基础库鉴权数据。
- 业务项目可每次启动调用 `setDefaultAuth()` 提供设备默认认证，用户保存过认证后基础库会优先使用已保存认证。
