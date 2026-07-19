# 发布检查清单

## 1. 文档一致性

发布前必须确认：

- README 已更新。
- docs 全部与实现一致。
- Profile 表与代码一致。
- API 文档与头文件一致。
- 资源预算已按当前构建产物核对。
- 实机测试结果已记录。
- [已知限制](10_known_limitations.md) 已更新。

## 2. 架构边界

确认：

- Core 不包含 Bus / WiFi / Web / OTA / Fs / Watchdog。
- Profile 数量固定为 7 个。
- 不引入静态模块注册表。
- 不引入复杂策略框架。
- 不引入未列入边界的新模块。

## 3. 编译检查

必须通过：

- 7 个 profile。
- ESP32 / ESP32-S3 / ESP32-C3。
- Arduino Core 2.x / 3.x。
- Core 3.x pioarduino 构建前运行 `scripts/ensure_arduino3_platformio.py`，并保留 env 的 `scripts/pioarduino_core3_preflight.py` 自动检查，确保 Arduino 3.x framework 主包和 libs 包已安装，避免 `FRAMEWORK_DIR` 为空的本地包状态错误。

检查：

- warnings。
- firmware size。
- text / data / bss。
- map 符号。
- 非法 `ESP32BASE_PROFILE` 必须编译失败。
- 错误依赖组合必须编译失败，例如启用 `OTA` 但关闭 `WEB`。
- 启用 `OTA` 时默认启用 `ARDUINO_OTA`；显式 `ESP32BASE_ENABLE_ARDUINO_OTA=0` 必须可关闭命令行 OTA。

## 4. 发布包检查

必须通过：

- `platformio pkg pack .` 成功。
- 发布包包含可选模块和 Web 运行所需源码。
- 发布包包含推荐分区表：`partitions/esp32-4mb-ota-balanced.csv`、`partitions/esp32-c3-4mb-ota-balanced.csv`、`partitions/esp32-s3-8mb-ota-balanced.csv`。
- 推荐分区表的 `app0` 偏移必须和 PlatformIO / Arduino 上传地址一致；默认应为 `0x10000`。
- classic ESP32 4MB 推荐分区表必须保持 `app0=0x10000`，不要求业务项目设置 `board_upload.offset_address`。
- 发布包包含 `examples/basic` 的 profile 依赖裁剪验证源码。
- 发布包包含独立 PIO 示例 `examples/full_demo`、`examples/web_ui_gallery`、`examples/web_logs_ota`、`examples/net_runtime`。
- 发布包不包含历史设计、评审、评估等本地非发布材料。
- 发布包不包含 `.pio/`、`.cache/`、`idf_component.yml` 等构建生成物。

## 5. 裁剪检查

- CORE profile 不能因为 Web 多编译单元被 `srcFilter` 编译而链接 WebServer、WiFi、Update、LittleFS 等非目标符号；先跑 `examples/basic -e esp32_core`，再跑 `scripts/check_trim_symbols.py`。

必须证明：

- CORE 不链接 WiFi/WebServer/Update/LittleFS。
- NET 不链接 WebServer/Update/LittleFS，除非显式启用 FS。
- WEB 不链接 Update。
- FS 关闭时 FileLog 不拉入 LittleFS。
- 关闭 Bus/Fs/Health 不产生静态对象。
- Web 可选页面资源必须按能力宏裁剪，避免未启用 App Events、FS、OTA 或 FileLog 时仍带入对应专属资源。
- 外部最小应用仅 `#include <Esp32Base.h>` 并启用 FULL profile 时，不额外声明 framework 内置库也必须编译通过，包括 ArduinoOTA。
- RTC 关闭时不链接 DS3231/PCF8563 驱动符号。
- DS3231 构建必须排除 PCF8563 驱动符号；PCF8563 构建必须排除 DS3231 驱动符号，证明两个 RTC 芯片是构建期二选一，不是运行时共存。

## 6. RTC / 时间源检查

必须通过：

- `pio test -e native_time_harness`。
- `pio test -e native_time_pcf8563_harness`。
- `pio run -d examples/rtc_time_source -e esp32_ds3231`。
- `pio run -d examples/rtc_time_source -e esp32_pcf8563`。
- DS3231 示例构建的 map/ELF 裁剪检查必须禁止 `pcf8563` / `Pcf8563`。
- PCF8563 示例构建的 map/ELF 裁剪检查必须禁止 `ds3231` / `Ds3231`。
- RTC 未启用的现有 profile 行为不应要求业务初始化 `Wire`，也不应改变 NTP-only 项目的对时语义。
- 启用 RTC 但目标板未接芯片时，`Esp32Base::begin()` 不能失败，Status/日志应显示 RTC 诊断异常。
- RTC 芯片寄存器必须按 UTC 日历字段读写；Web、日志、App Events 和文件 Last modified 展示使用统一固定偏移格式化。
- NTP 成功后按 `ESP32BASE_RTC_NTP_WRITEBACK` 和 `ESP32BASE_RTC_WRITEBACK_THRESHOLD_SEC` 决定是否写回所选 RTC，写回失败不得影响 NTP 时间生效。
- App Events、Web Status、Watchdog reset time、FS Last modified 和系统诊断日志不得继续把 NTP 当作唯一真实时间来源；应统一通过 `Esp32BaseTime` 读取或解析。
- 基础库不得占用 RTC 中断引脚，不得接管 DS3231/PCF8563 的闹钟、方波、定时器、温度或校准等扩展功能。
- 实机条件允许时，分别记录 DS3231 正常时间、DS3231 缺失/OSF，PCF8563 正常时间、PCF8563 缺失/低电压或时钟无效状态；无法实机覆盖时必须在发布记录中写明缺口。

## 7. OTA 检查

必须通过：

- Web Auth 开启时，未认证不能写 flash。
- Web Auth 关闭时，OTA route 仍注册且无密码保护。
- espota 正确密码上传成功。
- espota 错误密码上传失败且设备仍可访问。
- `pio run -t webota` 通过 HTTP Web OTA 上传成功。
- `webota` 错误 Web Auth 上传失败且设备仍可访问。
- Web OTA 与 espota 不得同时写 flash。
- SHA256 正确路径成功。
- SHA256 错误路径失败。
- `Update.end(true)` 只在 SHA256 通过后调用。
- Watchdog 长操作作用域保持当前 task WDT 注册，通过 yield 和 reset/feed 降低误触发。
- Config deferred flush pause/resume。
- WiFi power save off/restore。
- 中途断电不变砖。
- rollback 可用。
- 上传页进度显示正确。
- 上传页进度容量只显示 KB/MB/B 人性化值；状态/API JSON 保留 raw `bytes` 字段。

## 8. 配网检查

必须通过：

- 无凭证进入 AP config portal。
- 有凭证但连接失败时不自动进入 AP。
- STA 安全启动保护：制造 `sta_guard=true` + brownout/panic/watchdog reset reason 后累计 `sta_rst`；达到阈值后设置 `sta_pause=true` 并进入 AP config portal；重新提交凭据后清除 pause/guard/count。
- Web 在 config portal 下启动。
- DNS 拦截生效。
- 所有 DNS 查询通配到 AP IP。
- iOS / Android / macOS / Windows 可进入配网页。
- WiFi 配置页拒绝空 SSID，允许空密码用于开放 WiFi。
- WiFi 配置页拒绝超长 SSID / 密码。
- WiFi 配置页回显 SSID，不回显密码；空密码字段保留已保存密码，显式清空用于开放 WiFi。
- 提交凭证后切换 STA。
- 错误密码进入 backoff。
- 路由器恢复后重连。
- STA 已关联但上级 DHCP 暂时不可用时保持关联，不在普通 15 秒连接超时后反复调用 `WiFi.begin()`；DHCP 恢复后获得 IP 并启动 Web/mDNS，超过独立 DHCP 超时后才进入 backoff。
- 已连接后只丢失 IPv4 时发布断开事件并等待 DHCP 恢复；互联网或 NTP 单独不可用不得改变 WiFi `CONNECTED` 状态。

## 9. 存储检查

必须通过：

- namespace/key 长度校验。
- readonly 读取不创建 namespace。
- 写前比较。
- deferred delay。
- `flushAll()`。
- deferred 写后立即读返回 pending 新值。
- deferred pending/NVS 同值去重，不重复推迟 flush 或产生 NVS 写入。
- restart 前全部落盘。
- NVS 写满返回 false。
- `factoryReset()` 清理 `eb_wifi`、`eb_wifi_rcv`、`eb_web`、`eb_log`、`eb_ui`、`eb_sys.hostname`，并在条件跟踪启用时清理 `eb_app_events`。
- `factoryReset()` 保留 `eb_sys` 中的 boot/restart/watchdog 统计诊断 key。
- 单项清理 API 只影响对应配置范围；`clearSystemConfig()` 只清 hostname，不清统计诊断 key。
- namespace 不存在时出厂重置返回成功，不创建空 namespace。
- `factoryReset()` 不误删业务 namespace，不格式化 LittleFS，不删除 FileLog 日志文件内容。
- `factoryReset()` 只把NVS namespace确实不存在视为无需清理；namespace打开失败或清除失败必须返回false。
- `factoryReset()`成功后完整出厂重置流程必须重启设备；条件状态存在NVS待保存失败时，不得用App Events `reload()`替代重启。
- `clearLibraryNamespaces()` 与 `factoryReset()` 行为一致。
- `enableConfigAudit(true)` / `enableConfigReadAudit(true)` 在 `Esp32Base::begin()` 前开启时能覆盖基础库初始化配置读写。
- 未配置 `ESP32BASE_DEFAULT_HOSTNAME` 时默认 hostname 为 `esp32base`。
- 合法 `ESP32BASE_DEFAULT_HOSTNAME` 生效；非法默认 hostname 回退 `esp32base` 并输出 WARN。
- 合法 `eb_sys.hostname` 覆盖构建默认 hostname；非法持久化 hostname 被忽略并输出 WARN。
- `factoryReset()` 后重启清除 `eb_sys.hostname`，恢复构建默认 hostname。
- ESP32 / ESP32-S3恢复按键构建默认GPIO0，ESP32-C3默认GPIO9，默认10000ms；达到阈值立即进入配置热点，不清凭据、不在按下状态重启，同一次按下只触发一次。
- System页能持久化启用状态、合法GPIO和1～60秒长按时间到`eb_wifi_rcv.button`，保存后立即生效；非法、无内部上拉能力和已知Flash GPIO必须拒绝。

## 10. 系统诊断日志（FileLog）检查

必须通过：

- FS profile 默认启用 ERROR 系统诊断日志（实现/API 名称 `Esp32BaseFileLog`）。
- 非 FS profile 不强行启用 FileLog。
- 默认路径为 `/esp32base/logs/system.log`。
- 默认轮转为 `4 × 32KB`。
- ERROR 仅在 FileLog 模式为 ERROR/WARN/INFO 后写文件；WARN 仅在 WARN/INFO 后写文件；INFO 仅在 INFO 后写文件；DEBUG/VERBOSE 不能配置为系统诊断日志模式。
- INFO 使用 `1KB / 2s` 缓存。
- Web System 页只能设置 Off/ERROR/WARN/INFO，非法 POST 值必须失败。
- Web System 页切换 FileLog 模式时，审计日志应记录上一次模式和新模式。
- FileLog 配置模式开启但运行期因 FS 写入故障停写时，Status、System Logs、System 页必须区分写入故障和用户关闭，并说明已有日志可能仍可读取。
- FileLog 配置模式开启但 FS/init 前置条件不满足时，Status、System Logs、System 页必须区分前置条件不足和用户关闭。
- FileLog 模式为 OFF 时，System Logs 和 System 页必须说明新日志不会写入。
- FileLog OFF 后 System Logs 页面仍能查看已有历史 segment。
- `GET /esp32base/logs` 和 `GET /esp32base/logs/raw` 必须只读，不得主动 `flush()`、创建、清空、重建或改变 FileLog fault 状态；需要写入/清理/格式化的维护动作必须走 POST。
- System 页格式化 LittleFS 成功后必须触发 after-format 回调/事件，结果应说明格式化、重新挂载和 FileLog reload 状态；格式化失败不得触发。
- System 页格式化 LittleFS 成功并重新 mount 后，启用 App Events 时必须重新创建 `/esp32base/records/app-events.v2/`，并逐个reload已登记的当前业务Store；条件活动状态保留在NVS，后续append/read不得沿用格式化前的运行态，任一恢复失败不得提示整体成功。
- `setSerialLevel(NONE)` 后 Serial 不输出，但 FileLog 仍按当前模式写入。
- `setRuntimeLevel(NONE)` 后 Serial 和 FileLog 都停止。
- WARN/ERROR 立即写入。
- 文件满后轮转 current、`.1`、`.2`、`.3`。
- clear 幂等，清空后 current 可继续写入。
- restart、deep sleep、OTA success、rollback restart 前 flush。

## 11. 文件系统检查

必须通过：

- 任何启动路径都不得自动格式化 LittleFS。
- 首次挂载失败不 halt。
- 格式化 LittleFS 只能来自明确维护动作，例如 Web System 页 POST 或显式调用 `Esp32BaseFs::format()`。
- Web System 页格式化 LittleFS 只清 LittleFS，不清 WiFi、Web Auth、业务 namespace 或任何 NVS；业务统计、文件索引或缓存同步清理由应用 after-format 回调/事件自行处理。
- 读写删文件正常。
- 二进制读写正常。
- append 正常。
- `readBytes()` / `readBytesAt()` 支持分页读取，文件不存在和 offset 越界返回失败，EOF 短读返回实际长度；未到 EOF 却读出 0 字节必须返回失败。
- `writeFile()` / `writeBytes()` / `appendFile()` / `appendBytes()` 写后大小校验正常，非空写入后末端可读。
- `writeBytes()` / `appendBytes()` / `writeBytesAt()` 大块读写通过 `Esp32BaseFs` 分块 I/O 和 watchdog-friendly service 覆盖；业务不需要在存储类里重复拆分 Flash 写入。
- `createFixedFile()` 支持常见定长文件创建，首字节、中间字节和末尾字节填充值正确；非法路径、FS 未 ready、空间不足等情况返回 false 且不崩溃。
- `createFixedFile()` 初始化大文件时不得用 `appendBytes()` 做分块循环；必须避免大 heap 分配，并避免触发 task watchdog。
- `writeBytesAt()` 支持已有文件固定位置覆盖，文件不存在和写越界返回失败，不隐式扩展文件；覆盖后文件大小不变且覆盖范围可读。
- FS 已满或存在不可读文件时，Web 诊断返回错误不应触发 task WDT 重启。
- `listDirInfo()` 返回 name、size、isDir 和 Last modified epoch；`/esp32base/fs` 文件树应展示修改时间和可读性/维护状态。Last modified 不是创建时间，时间明显不可信时应明确标识。
- 启用RecordStore时，目录名包含记录类型和版本；不同Store、不同版本互不覆盖，容量由最大逻辑Store预算、固定负载和段头开销计算。
- 新段必须先完成并验证 `.tmp` 文件再重命名；已有正式段不得用临时文件覆盖。已有控制文件定义不匹配、目录成员未知或段范围重叠时进入结构故障，不自动重写。
- RecordStore双控制头损坏进入结构故障；单槽CRC错误跳过并进入degraded，读取绝不返回损坏数据。
- RecordStore启动扫描和reload必须按段批量遍历槽位，最新分页必须按段倒序批量读取；不得按记录反复打开文件，千条容量的Store不能造成数十秒启动延迟。
- `readLatest()`、`readById()`、`readStatus()`不得从只读路径隐式创建或重写Store。
- `clear()`通过双控制头提交逻辑可见边界，再尽力删除旧段；清空后保留递增ID并可继续写入，它不是安全擦除。
- Web登记当前业务Store后，System页必须显示各Store路径、状态和记录数，并通过独立红色 `Clear Business Records` 危险操作统一清空；App Events和未登记历史版本不得进入该列表。
- Business Records清空必须先全量预检：任一Store未初始化或结构故障时零修改；执行中失败时停止并报告已清空数。成功后原Store对象记录数为0、ID继续递增且可继续append/read；cleanup失败时旧记录保持不可见并报告degraded。
- App Events默认100KiB、单条48字节、估算容量2113条、实际按段保留约2029～2113条，并在业务Store之前创建。
- `/esp32base/records/**`在FS管理页可查看和下载，但禁止上传覆盖和直接删除。
- App Events HTML/API/CSV只输出有效语义字段和状态，不输出损坏槽位或内部CRC布局；HTML页面必须保持结构清楚的状态摘要、筛选、最新优先分页列表，以及每条记录可打开并覆盖全部公开字段的详情视图。
- `/esp32base/fs?manage=1` 对 `unreadable` 文件仍必须提供单文件删除入口，但不能提供下载入口。
- 单文件删除失败后应尝试截断为 0；如果因此释放可见文件占用，页面应区分“已清空”和“彻底失败”。
- 清理文件后如果 FileLog 处于写入故障保护，应重新加载当前 FileLog 模式以便恢复写入。
- listDir / mkdir / rmdir 正常。
- FS 失败不影响 WiFi/Web。

## 12. Watchdog / Sleep 检查

必须通过：

- Watchdog enable/feed。
- 故意阻塞触发 reset。
- reset count 跨 boot 记录。
- Sleep 前 flushAll。
- deep sleep 必须走统一生命周期流程。
- ESP32-C3 不支持 wake source 返回 false。

## 13. Web 检查

必须通过：

- Basic Auth。
- App Config help 按 UTF-8 字节计数，192 字节可注册，193 字节拒绝且日志明确报告 `reason=help_too_long`、实际长度和上限；不得静默截断。
- 状态 API。
- chip API。
- firmware API。
- Hostname API。
- WiFi 配置 API。
- System Logs 页面。
- System Logs clear POST + confirm + once + 303。
- System Logs clear 回到页面后显示成功/失败提示。
- FS/FileLog 不可用时 System Logs 页面应显示不可用原因，不提供误导性的日志内容。
- 业务示例中的副作用 POST 必须调用 `Esp32BaseWeb::checkPostAllowed(context)`；跨站 `Origin` 拒绝由 Web 行为测试或实机验证覆盖。
- 日志内容 HTML escape。
- 默认 Web 首页和导航开箱可用。
- 业务优先导航可设置 device name、home path、home mode、system nav mode。
- Footer bar 可切换 Off、Status only、Links + status；Off 不输出底部横条，Status only 只输出运行摘要，Links + status 输出系统入口和运行摘要。
- `addPage()` / `addNavItem()` 注册的业务入口进入业务导航且不重复业务首页入口。
- 内置 Status/WiFi/OTA/System Logs/Tools/Auth 标签可覆盖，用于本地化；底层枚举名仍为 `BUILTIN_LOGS`。
- Tools 维护页中的重启和格式化 FS 按钮都有二次确认。
- `POST /esp32base/system/business-records-clear` 必须需要 Basic Auth、POST 和同源检查；GET不得清空，业务页面不应再提供平行的业务记录清空入口。
- Tools 维护页可保存 hostname，显示当前值、默认值、已保存值和重启需求；保存后不热切换当前运行时 hostname。
- 启用 `ESP32BASE_ENABLE_APP_CONFIG` 后，System 页显示 App Config 入口，`/esp32base/app-config` 可按 group 展示业务参数。
- 启用 `ESP32BASE_ENABLE_APP_EVENTS` 后，系统导航显示 App Events 入口，`/esp32base/app-events`、`/esp32base/api/app-events`、`/esp32base/app-events.csv` 可用；内置页面的状态、筛选、分页列表和逐条详情在桌面及窄屏下均不得挤压、重叠或丢失字段。
- App Events默认启用条件跟踪；持续观察、瞬时失败、确认生效、恢复、再次发生、多条件隔离、相同离散事件、`millis()`回绕、跨重启、NVS失败、Event Store失败、清空/格式化/reload、忘记活动/非活动/全部状态及替换tracker均有原生测试。NVS同值写必须跳过，错误类型不得被同值判断掩盖。显式关闭 `ESP32BASE_ENABLE_APP_EVENT_CONDITIONS` 后仍能编译离散事件，且不得保留条件NVS和Web状态符号。
- `POST /esp32base/system/app-events-clear` 必须需要 Basic Auth、POST 和同源检查；GET 不得清空，App Events 页面不得保留清空入口。清空只删除事件历史，必须保留NVS条件活动状态并在页面明确提示。
- 业务 `METHOD_ANY` 危险 handler 调用 `Esp32BaseWeb::checkPostAllowed(context)` 时，GET 必须返回 405 且不执行副作用。
- `/esp32base/logs` 不包含 App Events，仍只展示 FileLog 系统日志。
- App Config 注册校验覆盖非法 namespace、重复 `ns/key`、非法长度/范围/step/decimal scale/enum option 和超容量。
- App Config POST 必须服务端重新校验；字段级 validator 和页面级 validator 失败时零写入、零 change 回调。
- App Config 保存前 veto 必须使用页面级 validator；回调可读取全部提交值，拒绝时不得写入任何 App Config NVS 字段，也不得触发 save callback。
- App Config 保存只写变化字段，未变化字段不写 NVS；旧页面 revision 提交被拒绝。
- App Config 修改 `restartRequired` 字段后，未重启会话内重新进入页面仍显示待重启提示、运行中旧值和已保存新值；改回旧值后提示消失。
- App Config callback 能拿到正确旧值和新值；decimal raw、bool、enum 值语义正确。
- 格式化 FS 等破坏性维护操作必须输出 WARN 级日志，记录发起、结果和关键恢复步骤。
- 自定义路由 begin 前注册。
- 自定义路由 Web ready 后注册。
- JSON escape。
- 内置页面大字节数只显示 KB/MB/B 人性化值；状态/API JSON 保留 raw `bytes` 字段。
- 慢请求日志。
- 长 handler 限制已文档化。

## 14. Examples 检查

必须通过：

- `examples/basic` 继续覆盖 profile/芯片/Core 版本矩阵。
- `examples/basic` 的 profile 依赖裁剪验证源码继续生效。
- `examples/full_demo` 可在自身目录 `pio run`。
- `examples/full_demo` 覆盖 App Config string/int/decimal/bool/enum、字段级校验、页面级校验、重启提示和回调。
- `examples/web_ui_gallery` 可在自身目录 `pio run`。
- `examples/web_ui_gallery` 覆盖 Web UI baseline 的状态、统计、分页记录、配置、命令、分步操作、维护、访问控制、确认、空状态和表单页面。
- `examples/web_logs_ota` 可在自身目录 `pio run`。
- `examples/net_runtime` 可在自身目录 `pio run`。
- `examples/app_events_demo` 可在自身目录 `pio run`，并演示 App Events 写入、分页查看、JSON/CSV 和 POST 写入。
- `examples/record_store_demo` 至少完成 ESP32 / ESP32-S3 / ESP32-C3 和 Arduino Core 3.x 构建，并演示固定payload编码、动作开始快照、完成记录和最新优先读取。
- `examples/rtc_time_source` 可在自身目录分别构建 DS3231 和 PCF8563 env，并清楚演示构建期二选一、I2C 初始化所有权和业务使用 `Esp32BaseTime` 的接入方式。
- 所有启用 FS 的示例不应通过构建参数覆盖系统诊断日志默认模式；默认保持 ERROR，现场排查时再显式切到 WARN/INFO。

## 15. Soak 检查

必须完成 48 小时 soak：

- FULL profile。
- ESP32 / ESP32-S3 / ESP32-C3 各一台。
- 高频 deferred NVS 写入。
- 周期 Web 状态查询。
- RTC 项目按低频或默认不轮询策略运行，不应在 loop 中产生高频 I2C 访问。
- 周期 `health.tick` bus 事件。
- Health tick 默认只输出 DEBUG；超过 loop 阈值才输出 INFO。
- 路由器掉电恢复。
- 周期读取 System Logs 页面。
- 无意外重启、卡死、heap 持续退化或重连失败。
