# Web 与配网

## 1. 定位

Web 层用于小型局域网管理，不是大型 Web 平台。

页面结构、样式基线、业务页面模板、换肤策略和视觉调整规则见 [Web UI 页面结构与样式基线](11_web_ui_baseline.md)。

能力：

- WebServer。
- Basic Auth。
- 状态 API。
- WiFi 配网页面/API。
- System Logs 页面。
- 应用自定义路由。
- JSON helper。

不做：

- HTTPS。
- 多用户权限。
- WebSocket。
- SPA。
- 大型文件管理器。

## 2. 启动条件

Web 依赖 WiFi。

Web 启动条件：

```text
WiFi connected OR WiFi config_portal
```

这保证：

- STA 已连接时可访问管理页面。
- AP 配网模式下也能访问 WiFi 配网页面。

## 3. Captive Portal

配网模式需要：

- `Esp32BaseWiFi` 启动 AP。
- `Esp32BaseDns` 启动 DNS 拦截。
- `Esp32BaseWeb` 启动 WiFi 配网页面。

DNS 拦截策略：

- 对所有 DNS 查询返回 AP IP。
- 不维护固定域名白名单。
- 不提供 `addCaptiveTarget()` 扩展 API。
- OS captive portal 探测路径由 Web 层响应，例如 `/generate_204`、`/hotspot-detect.html`、`/ncsi.txt`。

必须实测：

- iPhone
- Android
- macOS
- Windows

## 4. 认证

默认：

- Web Auth 开启。
- 默认开发账号密码可以存在。
- 应用可在 `Esp32Base::begin()` 前调用 `Esp32BaseWeb::setDefaultAuth()` 设置默认认证。
- 认证优先级为：已保存认证 > 应用默认认证 > 库默认 `admin/admin`。
- 业务项目需要允许用户修改认证账号/密码时，优先链接内置 `/esp32base/auth` 认证管理页面。
- 业务自建配置页时，应先 `checkAuth()`，再用 `verifyAuth(currentUser, currentPass)` 验证当前凭据，最后调用 `saveAuth(newUser, newPass)`。
- 更新 Web Auth 后立即生效；浏览器缓存旧 Basic Auth 时，后续请求会重新触发认证。
- Web Auth 持久化使用 `eb_web.auth_user`、`eb_web.auth_pass`，明文密码用于启动日志和调试。
- `INFO` 日志明文输出 Web 用户名和密码，这是本库用于业务接入和现场调试的设计。
- Basic Auth 请求日志每个 HTTP 请求最多输出 1 条，避免 OTA 上传分块反复校验时刷屏。

OTA 规则：

- OTA route 按 profile/OTA 编译条件注册，不依赖 `setDefaultAuth()` 或持久化认证。
- Web Auth 开启时，OTA 页面和上传接口复用 Web Basic Auth。
- Web Auth 关闭时，OTA 页面和上传接口不要求认证，风险由应用和用户自行承担。
- 调用 `Esp32BaseWeb::setAuthEnabled(false)` 后，内置 HTTP 路由会完全开放，包括 WiFi 保存/清除、Auth 保存、重启、System、System Logs clear 和 Web OTA；这只适合受控调试网络，不适合暴露到外部网络。
- 启用 OTA 时默认同时启用 ArduinoOTA/espota；espota 只使用当前 Web Auth 密码作为 `--auth` 密码，即使 Web Auth 关闭也仍要求密码。

不使用“密码是否等于默认字符串”作为唯一判断。

安全说明：

- HTTP Basic Auth 明文传输。
- 它只用于防误操作。
- 不抵御 LAN 内主动攻击。
- 关闭 Web Auth 时，内置页面和 OTA 均无密码保护。
- 关闭 Web Auth 不只是关闭登录框，而是让所有内置 HTTP 页面和 POST 操作无需认证。
- 关闭 Web Auth 只影响 HTTP/Web OTA；不关闭 ArduinoOTA/espota 密码。
- Web Auth 密码不在 HTML、JSON 或 API 响应中输出；INFO 日志会明文输出 Web 用户名和密码，日志访问权限由应用和部署环境控制。
- 内置危险 POST 必须是 POST method，并会做轻量 `Origin` / `Referer` host 校验；存在这些头时必须与请求 `Host` 同源，缺失时放行，保证 curl、PlatformIO `webota` 和简单脚本可用。非 POST 返回 405，校验失败返回 403，不执行副作用。

## 5. 路由表

规则：

- 固定容量。
- 应用路由可在 `Esp32Base::begin()` 前注册。
- 应用路由可在 Web 启动前注册。
- Web 已启动后注册的路由应立即生效。
- route path 必须以 `/` 开头，最大可见长度为 47 字节。

机制：

- `Esp32BaseWeb::addRoute()` 写入静态路由表。
- Web 启动时把缓存路由注册到 `WebServer`。
- Web 已 ready 时，新增路由立即注册。
- `METHOD_ANY` 路由进入应用 handler 前会先建立请求上下文，应用不需要直接访问底层 `WebServer`。
- handler 内 `Esp32BaseWeb::currentMethod()` 可区分 `METHOD_GET` / `METHOD_POST`；handler 外返回 `METHOD_UNKNOWN`。
- `addPage()` 注册业务页面并进入业务导航；`addNavItem()` 只注册导航项，不注册路由。
- `setHomeMode()` 控制 `/` 和 `/esp32base` 的首页模型：基础库首页、业务首页优先或融合首页。
- `setSystemNavMode()` 控制 Status、System Logs、System 等系统入口在顶部、底部或底部紧凑系统工具区展示；默认使用底部紧凑系统工具区，让顶部主要服务业务导航。
- `setFooterBarMode()` 控制 `sendFooter()` 输出的底部横条：Off 不显示，Status only 只显示右侧运行摘要，Links + status 同时显示左侧系统入口和右侧运行摘要。
- `setBuiltinLabel()` 可覆盖 Status/WiFi/OTA/System Logs/App Events/System/Auth 标签，用于应用统一本地化；系统诊断日志底层枚举仍是 `BUILTIN_LOGS`，系统维护页统一使用 `BUILTIN_TOOLS`，不提供旧 Reboot 历史别名。
- `setHeadExtraCallback()` 可在 `sendHeader()` 的 `</head>` 和顶部导航输出前注入业务 CSS，业务页面不需要复制基础库 header/nav；`/esp32base` 及其子路径的内置页面会跳过该业务注入，避免内置页重复下发业务样式。
- 导航按当前请求路径输出 `active` class：完全匹配优先，嵌套路由按最长 path 前缀匹配；`SYSTEM_NAV_SECTION` 下系统维护入口只在 footer 中展示，WiFi/Auth/OTA 二级页会把 System 标记为 active，App Config 页面在启用时标记自己的 footer 入口。
- 默认 Web 样式采用简洁中性色，普通链接、导航和 tabs 不使用蓝色主色；业务项目最终视觉仍由 `setHeadExtraCallback()` 注入 CSS 覆盖。
- 业务页面应优先使用 `docs/11_web_ui_baseline.md` 中定义的页面能力块、helper 和基础 CSS。找不到合适能力块时，不应先在业务项目复制 CSS 或写一次性布局补丁，而应回到 Esp32Base 评估是否补充统一能力块、CSS 类、helper、示例和文档。

默认容量：

```cpp
#define ESP32BASE_WEB_MAX_ROUTES 24
```

该默认值覆盖常见业务页面/API 增长，不要求业务项目一开始就为 route 表单独调参。route 较少且需要节省静态 RAM 的应用，可以在构建参数中显式调小 `ESP32BASE_WEB_MAX_ROUTES`；页面/API 继续增长时也可以按项目显式调大。

## 6. 内置路径

Web:

- `GET /esp32base`
- `GET /esp32base/api/status`
- `GET /esp32base/api/chip`
- `GET /esp32base/api/firmware`
- `GET /esp32base/api/hostname`
- `POST /esp32base/api/hostname`
- `GET /esp32base/wifi`
- `POST /esp32base/wifi`
- `POST /esp32base/api/wifi`
- `POST /esp32base/api/wifi/retry`
- `POST /esp32base/api/wifi/clear`
- `GET /esp32base/auth`
- `POST /esp32base/auth`

OTA:

- `GET /esp32base/ota`
- `POST /esp32base/ota`，浏览器表单使用 multipart 上传
- `POST /esp32base/ota/raw`，PlatformIO `webota` 使用 `application/octet-stream` 直接上传固件原文
- `GET /esp32base/api/ota`

System Logs:

- `GET /esp32base/logs`
- `GET /esp32base/logs/raw?segment=N`，以 `text/plain; charset=utf-8` 流式输出单个系统诊断日志 segment，供 System Logs 页面 iframe 和原文查看使用
- `POST /esp32base/logs/clear`，保留直达入口，成功后回到 System Logs 页面

App Events，仅 `ESP32BASE_ENABLE_APP_EVENTS=1`：

- `GET /esp32base/app-events`，独立应用业务事件页面，不混入系统诊断日志；支持 `page/per/level/time/source/type/reason/q` 筛选参数，非法筛选返回 `400 invalid_filter`。
- `GET /esp32base/api/app-events?offset=0&limit=50`，按最新优先分页输出 JSON；支持 `level/time/source/type/reason/q` 筛选参数，事件对象同时包含 `uptimeSec` 和 64-bit 派生 `uptimeMs`，非法筛选返回 `400 {"ok":false,"error":"invalid_filter"}`。
- `GET /esp32base/app-events.csv`，导出当前筛选后的应用事件 CSV；非法筛选返回 `400 invalid_filter`。

System:

- `GET /esp32base/tools`
- `GET /esp32base/fs`，仅 `ESP32BASE_ENABLE_FS=1`，LittleFS 文件占用诊断；默认只读，文件树提供单文件下载，`?manage=1` 进入单文件删除管理模式
- `GET /esp32base/fs/check?dir=/data&name=file.bin`，仅 `ESP32BASE_ENABLE_FS=1`，上传前检查目标路径；上传保留本地文件名，可选择任何已有目录，不创建目录
- `GET /esp32base/fs/download?path=/file`，仅 `ESP32BASE_ENABLE_FS=1`，下载一个 LittleFS 文件；需要认证，目录和非法路径不会下载
- `POST /esp32base/fs/delete`，仅 `ESP32BASE_ENABLE_FS=1`，删除一个 LittleFS 文件；需要认证和同源 POST，成功或失败后 303 回到管理模式
- `POST /esp32base/fs/upload`，仅 `ESP32BASE_ENABLE_FS=1`，上传一个 LittleFS 文件；需要认证和同源 POST，目标存在时必须显式覆盖，可写入任何已有目录
- `GET /esp32base/app-config`，仅 `ESP32BASE_ENABLE_APP_CONFIG=1`
- `POST /esp32base/app-config`，仅 `ESP32BASE_ENABLE_APP_CONFIG=1`
- `POST /esp32base/tools/hostname`
- `POST /esp32base/tools/reboot`
- `POST /esp32base/tools/footer-bar`
- `POST /esp32base/tools/filelog`
- `POST /esp32base/tools/logs-clear`，System 页面主入口，成功后回到 System 页面
- `POST /esp32base/tools/app-events-clear`，仅 `ESP32BASE_ENABLE_APP_EVENTS=1`，清空事件日志；System 页面危险操作入口，成功后回到 System 页面
- `POST /esp32base/tools/watchdog-trip-reset`
- `POST /esp32base/tools/format-fs`
- `POST /esp32base/api/restart`

OTA 路由只在启用 OTA 且认证条件满足时注册。

## 7. 内置页面交互

WiFi 配置页：

- 回显当前 SSID。
- 回显当前密码。
- 页面使用 baseline 表单结构：`formpanel + editform + fieldgrid`；清除 WiFi 使用 `dangerpanel`。
- 表单提交前用前端 JS 校验 SSID 非空；密码可为空，用于开放 WiFi。
- 服务端 API 必须重复校验 SSID 非空，密码允许为空。
- 空 SSID 返回错误，不保存凭证。
- SSID 超过 32 字节或密码超过 64 字节时返回错误，不静默截断。
- `/esp32base/wifi` 和 `/esp32base/api/wifi` 都是表单提交端点，成功/失败使用 303 跳转到 HTML 页面；它们不是 JSON API。
- 当已保存凭据因 STA 安全启动保护暂停时，页面显示恢复说明和 guarded reset 计数，并提供“恢复并重试已保存 WiFi”操作；该操作调用 `/esp32base/api/wifi/retry`，不要求用户重新保存同一组密码。

System 维护页：

- 默认底部系统导航展示 Status、System Logs、System；`System Logs` 对应 `/esp32base/logs`，是系统诊断日志查看入口，底层实现仍为 `Esp32BaseFileLog`。启用 App Events 时额外展示 App Events 直达入口，启用 App Config 时额外展示 App Config 直达入口。WiFi、Auth、OTA 是低频配置/维护入口，收在 System 页面中并显示为 WiFi Setup、Web Auth、Firmware OTA，但保留原直达 URL。
- System 页面使用 baseline 分块：低频入口使用两列入口网格，Open 按钮固定为紧凑浅按钮并贴齐右侧列，不能跨列或遮挡说明；手机端保持“说明 + Open”左右结构；设置项和维护项在 PC 上用两列 `toolgrid` 收敛高度，手机端自然单列；可编辑基础参数使用 `formpanel`，普通维护项使用 `actionpanel`，重启、格式化、清日志等危险操作使用 `dangerpanel`。
- 启用 App Config 时，System 页面首位仍显示 App Config 入口；App Config 是业务持久化参数配置页，不和基础库维护参数混在 System 长页面中。
- Hostname 设置区显示当前 hostname、构建默认 hostname、已保存 hostname 和是否需要重启；保存只写入 `eb_sys.hostname`，不热切换当前 DHCP hostname、mDNS、OTA 或 Web 身份，页面必须提示重启后生效。
- Footer bar 模式设置只接受 Off、Status only、Links + status，保存后立即生效并写入 `eb_ui.footer_mode`；该设置只控制底部横条，不关闭直达 URL 或顶部业务导航。
- 重启按钮必须有二次确认，并通过统一 lifecycle restart 执行；POST 响应必须替换浏览器历史到 GET URL，避免刷新重复提交。
- `/esp32base/api/restart` 是脚本兼容入口，返回纯文本并立即进入重启流程，不提供 JSON 错误模型。
- 重启和格式化等危险操作必须分组显示，避免按钮与下一项标题贴得太近。
- 启用 Watchdog 的 profile 显示 Watchdog lifetime/trip 小计维护；当 `wdt_trip_base` 大于 lifetime 时显示 `invalid baseline`，Reset Watchdog Trip 写入并回读确认 `eb_sys.wdt_trip_base` 和 `eb_sys.wdt_trip_time`，不清 `eb_sys.wdt_cnt`。
- 启用 FS 的 profile 显示 `Format LittleFS`，该操作会删除日志和所有 LittleFS 文件，但不清除 WiFi、Web Auth 或 NVS 配置。
- 启用 FileLog 的 profile 显示系统诊断日志模式设置、运行态和 `Clear system logs`；模式设置只接受 OFF、ERROR、WARN、INFO，保存后立即生效并写入 `eb_log.mode`，清空日志只接受 POST，表单使用 `confirm()` 和 `once(form)`，成功后回到 System 页面显示结果。运行态为 `write fault` 时使用 WARN notice，表示配置模式仍开启，但 FileLog 因 FS 写入故障被运行期保护停写；已有日志仍可能可读，不应显示成 `disabled`。运行态为 `unavailable` 时使用 WARN notice，表示配置模式开启但 FS/init 前置条件未满足。运行态为 `disabled` 时使用 INFO notice，明确 FileLog 模式为 OFF，新日志不会写入。
- 系统诊断日志默认文件为 `/esp32base/logs/system.log`，App Events store 默认文件为 `/esp32base/app-events/events.bin`；二者都位于 `/esp32base/**` 基础库管理命名空间。
- 启用 App Events 的 profile 显示 `Clear App Events` 危险操作；它只清空 `/esp32base/app-events/events.bin` 应用业务事件日志，不清系统诊断日志、WiFi、Web Auth、NVS 配置或其他 LittleFS 文件。
- 格式化 FS 是显式 POST 操作；执行前 flush 系统诊断日志，成功后重新 mount FS、重新加载 FileLog 模式，并在 App Events 启用时重新创建 `/esp32base/app-events/events.bin`；成功提示应明确显示在 System 页面，同时输出 WARN 级维护日志记录请求、format/mount/FileLog reload/App Events recreate 结果。重启请求同样输出 WARN 级维护日志。
- 普通 `GET` 页面慢请求只输出 DEBUG；`POST` 等操作慢请求输出 INFO，默认 WARN 系统日志不记录，现场排查时可切到 INFO 查看。
- API 层仍建议要求 POST，不使用 GET 触发重启。
- 内置危险 POST 包括 WiFi 保存/清除、App Config 保存、Hostname 保存、Auth 保存、重启、System 操作、System Logs clear、Web OTA upload/done；跨站 `Origin` 或 `Referer` 会被拒绝。
- 业务自定义 POST 或危险操作应优先调用 `Esp32BaseWeb::checkPostAllowed(context)`，不要只做 `checkAuth()`；即使业务误把危险 handler 注册为 `METHOD_ANY`，该 helper 也会拒绝非 POST 请求。

Status 页：

- `/esp32base` 默认作为只读设备体检页，不承载配置保存和危险操作。
- 第一屏不再额外做 `System Overview` 预览块，避免和相邻详细分区重复；常用信息直接按 Device、Network、Runtime Health、Storage & Logs、Firmware & OTA、Hardware 排序。
- 同一数据不应在相邻分区同名同值重复展示；需要高频查看的信息应前置为正式分区，而不是再做一层摘要。
- Partition Table 作为最后的低频详细表。
- 系统诊断日志（`Esp32BaseFileLog`）属于 Storage & Logs，不作为健康摘要项；日志级别、当前文件大小和路径应拆开显示，避免把 WARN/INFO 等日志级别误读为健康状态。

App Config 页面：

- 仅在 `ESP32BASE_ENABLE_APP_CONFIG=1` 时注册，固定路径为 `/esp32base/app-config`。
- 业务必须在 `Esp32Base::begin()` 前用 `Esp32BaseAppConfig` 注册 group 和字段；字段直接绑定业务 `Esp32BaseConfig` namespace/key；注册传入的字符串和 enum option 数组必须保持固件生命周期有效。
- 启用后必须显式设置 `ESP32BASE_APP_CONFIG_MAX_GROUPS` 和 `ESP32BASE_APP_CONFIG_MAX_FIELDS`，硬上限为 16 组、128 字段。
- 未注册 group 或字段时页面显示轻量空状态，提示应用尚未注册配置字段。
- 支持 string、int、decimal 定点数、bool、enum；string 最大 256 bytes，enum value 最大 31 bytes，label/option label 最大 31 bytes，help 最大 96 bytes，unit 最大 12 bytes，decimal 使用 `int32_t raw` 和 `scale=0..6`。
- 页面按 group 输出紧凑 panel，字段默认直接可编辑；点击 Save 时前端只展示变更核对清单，不作为服务端事实来源。
- 变更核对清单使用中性 review 样式，不使用成功色或警告色；只有实际保存结果才使用 `ok`/`danger` notice。
- POST 保存时后端重新解析、执行内置校验、执行字段级和页面级业务校验、读取当前 NVS 并计算实际变化字段。
- 任一校验失败时零写入；校验通过后只保存变化字段，未变化字段绝不写 NVS。
- 校验通过后若发生 NVS 写入失败，已成功写入的字段不会回滚；`ChangeCallback` 只对成功字段触发，整次保存结束的 `SaveCallback` summary 会报告失败数量，页面返回 partial 提示。
- 保存成功字段会触发 `ChangeCallback`，回调包含旧值和新值；整次保存结束触发 `SaveCallback` summary。
- 页面带 RAM revision；旧页面提交会被拒绝并提示刷新，避免覆盖已经变化的配置。
- 字段可标记 `restartRequired`；保存后不会自动重启，但未重启会话内页面会持续提示这些字段仍待重启生效，并显示运行中旧值和已保存新值；极低内存下 string/enum 旧值可能显示为 `unavailable`，提示仍保留到重启。
- 第一版不提供单字段弹窗、多配置页、敏感字段隐藏或 CSRF token；安全边界为 Web Auth、POST only 和 Origin/Referer 同源检查。

OTA 上传页：

- 使用 Web Basic Auth，不额外要求单独认证。
- 页面使用 `formpanel uploadpanel`，保持上传控件、SHA256 可选校验和进度反馈在同一任务块内。
- 上传中显示进度。
- 页面进度同时显示百分比、已处理容量和总容量。
- 页面容量值只显示 KB/MB/B 人性化格式；OTA 状态 API 继续保留 raw `bytes` 数值并附带 `human` 字段。
- 上传成功后提示设备正在重启，并在短暂等待后跳转到当前配置的首页。
- 上传失败时留在 OTA 页面，显示失败原因，并允许用户重新选择固件上传。

System Logs 页面：

- 需要 Basic Auth。
- FS/FileLog 不可用时显示轻量不可用面板，标题为 `System logs unavailable`。
- Clear system logs POST 后通过 303 回到 System Logs 页，并在页面顶部显示成功或失败提示；刷新页面不重复提交。
- FileLog 模式为 OFF 时，System Logs 页面仍展示已有历史日志；OFF 只表示停止后续写入。
- `GET /esp32base/logs` 和 `GET /esp32base/logs/raw` 必须保持只读，只读取已经落盘的系统诊断日志快照，不主动 `flush()`、创建、清空或重建文件。页面会展示当前 buffer used/total，尚在缓存中的 INFO 日志可等待常规 flush interval；清空、格式化、重启等维护副作用仍必须通过 POST，不通过 GET 触发。
- System Logs 页面只面向 Esp32Base 系统诊断日志，不读取 `Esp32BaseAppEventLog`，也不展示业务事件。页面命名面向用户，`Esp32BaseFileLog` 是底层实现/API 名称。
- 显示 enabled、path、mode、rotate files、buffer used/total、flush interval、max per file、max total、每段大小。
- 系统诊断日志状态信息使用 panel 内紧凑小字号表格展示，label/value 纵向对齐；其中的容量值只显示 KB/MB/B 人性化值，不重复 raw bytes。
- 页面顶部以标签展示所有 segment，顺序为 `current-0`、`history-1`、`history-2` 到最旧 history；标签里的文件名和大小分开展示，大小只显示 KB/MB/B 人性化值。
- 默认显示 `current-0`；可通过 `?segment=N` 查看单个历史文件，非法或越界 segment 回落到 `current-0`。
- 日志正文不内联进主 HTML；System Logs 页面通过 iframe 加载 `/esp32base/logs/raw?segment=N` 的 `text/plain` 原文，避免大日志逐字符 HTML escape。
- 清空日志入口位于 System 页面；System Logs 页面只负责查看日志。
- 日志正文一次只展示一个 segment，包括 0 字节文件；非法或越界 segment 回落到 `current-0`。
- raw 日志内容不折叠、不省略、不规整空白行，页面展示应忠实反映文件内容。
- 日志正文流式输出期间必须周期性 yield/feed Watchdog，避免大日志页面长请求触发看门狗。

App Events 页面：

- 仅在 `ESP32BASE_ENABLE_APP_EVENTS=1` 时注册，标题和导航标签为 `App Events`，可通过 `setBuiltinLabel(BUILTIN_APP_EVENTS, "...")` 覆盖。
- 这是应用业务事件日志的内置系统视图，用偏底层的维护视角展示事件日志内容、管理和存储状态；它不属于 System Logs，也不替业务系统解释业务语义。
- 页面显示 record/capacity、实际文件大小、有效事件数、next id、存储路径、expected size、head、active header、sequence、record size、紧凑筛选和 CSV 导出、分页表格和详情弹层；分页使用 `sendPagination()`，默认每页 10 条。
- 筛选条件包括等级、时间类型、来源、类型、原因和关键词；HTML、JSON API、CSV 共用同一组筛选语义。`source/type/reason` 必须符合事件 token 的字符和长度约束，避免超长参数被截断后误匹配。筛选请求不额外做独立 count 扫描，而是在输出当前页时统计匹配总数。
- 表格展示 `Status`、slot/index、ID、Time、Level、Event、Object、Details 和详情入口。CRC 损坏、magic 异常、level 异常、未提交或空槽等只要能读出 record 字节就客观展示并标记 status；字符串字段按固定长度安全显示，不假设损坏记录带有 `\0`。
- 详情弹层展示 `magic/id/epochSec/bootId/uptimeSec/value1/value2/value3/code/level/flags/valueMask/reserved/crc16/source/type/reason/object/text` 全部 record 字段，并展示 `slot/index/recordOffset/status/stored crc/calculated crc/readOk/magicOk/levelOk/crcOk/committed/resolvedEpochSec/uptimeMs` 等派生信息；内部字段使用弱化样式。
- `epochSec` 可用时按本地时间显示；否则如果本次 boot 后续 NTP 已同步，则解析并显示真实时间；仍无法解析时显示 `uptime N ms` 和 `boot N`，不伪造日期。
- 清空事件日志是危险操作，只在 System 页面提供 `POST /esp32base/tools/app-events-clear`，必须通过 Web Auth 和同源检查，成功后 303 回到 System 页面。
- JSON API 事件包含 `epochSec`、`resolvedEpochSec`、`bootId`、`uptimeSec` 和 64-bit 派生 `uptimeMs`；`resolvedEpochSec=0` 表示没有可信真实时间。
- JSON API 继续只输出有效事件，适合业务系统创建自己的业务事件列表和详情页；业务页面应使用业务语言解释事件，只展示业务需要的字段，不展示 `magic/crc16/reserved/valueMask/flags` 等内部字段。
- JSON API 和 HTML 页面都必须使用统一 JSON/HTML escape；CSV 导出使用 CSV escape 和 spreadsheet formula 前缀防护，保留当前筛选条件，并包含 slot/status/crc 等存储字段。CSV 读取失败时必须在输出中追加可见错误行，避免客户端误以为 200 响应是完整导出。
- 该页面明确和 `/esp32base/logs` 分离：`/esp32base/logs` 是 System Logs，展示 `Esp32BaseFileLog` 系统诊断日志，记录启动、联网、OTA、基础库运行等系统信息；`/esp32base/app-events` 是应用业务事件日志，记录应用显式写入的业务事件。
- 应用页面不应把 boot/reset、WiFi、NTP、OTA、LittleFS、FileLog fault 或基础库健康状态重复写入 App Events；这些系统诊断信息继续由 Status、System diagnostics 和 FileLog 展示。同一故障如果同时影响业务，App Events 应只表达业务影响、保护动作、跳过原因、用户维护结果或外部决策结果。App Events 页面只用于查看应用因业务原因写入的事件。

状态页/API：

- 内置系统首页的 heap、flash、FS、FileLog 容量只显示 KB/MB/B 人性化格式，不重复 raw bytes。
- OTA 页面进度只显示 KB/MB/B 人性化格式；API 大数字字节数可继续包含 raw `bytes` 和 `human`。
- JSON 字段建议同时提供 `bytes` 和 `human`，避免前端重复实现单位转换。

系统首页：

- `/esp32base` 是只读设备体检页，采用诊断优先结构：按 Device、Network、Runtime Health、Storage & Logs、Firmware & OTA、Hardware、Partition Table 展示调试信息，不额外显示相邻重复的总览预览块。
- Device 显示名称、hostname、固件、profile、uptime 和 boot count；Network 显示 WiFi/IP/RSSI、power save、STA MAC 和 AP MAC。
- Runtime Health 显示 heap free/min/max alloc/total、Watchdog、NTP time、last reset 和 last wake；heap 与 Watchdog 的多值信息使用紧凑子指标展示，避免逗号串联造成阅读困难。
- Storage & Logs 显示 FS used/free/total、Details 入口、文件/目录数量、已统计文件大小、other/overhead、FileLog enabled/disabled/unavailable/write fault、日志级别、当前文件大小、日志总占用/上限和路径；File details 入口并入 FS 行，FileLog level/current/used/limit 合并为一行子指标，避免右侧卡片明显高于 Runtime Health；Top 文件列表只放在 `/esp32base/fs` 详情页，避免状态页被低频诊断明细撑高；Hardware 显示芯片型号、revision、CPU、SDK、Flash、PSRAM 和 eFuse MAC。
- `/esp32base/fs` 默认是只读 LittleFS 详情页，显示 Summary、Top 10 最大文件和最多 128 项文件树；文件树提供单文件下载；当文件声明有大小但首块无法读取时，Action 显示 `unreadable`，下载路由返回 `500 File read failed`，避免浏览器保存 0 字节伪成功文件；当 FS used 明显大于可见文件合计时显示内部/历史占用告警，提示删除可见文件不一定释放全部空间。业务侧如果使用 `Esp32BaseFs` 读写文件，也必须检查返回值；逻辑大小存在不代表内容块一定可读。
- `/esp32base/fs?manage=1` 增加单文件删除和受限上传，不提供目录删除、批量删除、编辑、重命名、移动或任意路径输入；删除必须通过 `POST /esp32base/fs/delete -> 303 -> GET`，格式化仍只在 System 页危险操作区。上传流程是“选择已有设备目录 + 选择本地文件”，上传保留本地文件名，不创建目录，可选择任何已有目录；同名文件由 `/esp32base/fs/check` 触发浏览器确认后再带 `overwrite=1` 上传；路径过长会拒绝，不截断。上传完成会校验最终文件大小和末端可读性；上传到 FileLog 路径前会先 flush，上传结束后重新加载 FileLog 运行态。App Events 启用时，`/esp32base/app-events/events.bin` 显示为 `app events store`；其他 `/esp32base/**` 文件显示为 `esp32base managed`。这些标签只作为基础库命名空间提醒，不作为通用上传或删除禁区，便于测试和维护实验；上传或删除后会重新加载 App Events 运行态。不可读文件在管理模式仍显示删除入口，因为删除只依赖路径存在且是文件，不要求内容块可读。底层删除失败后会尝试截断文件为 0；删除或清理成功后若 FileLog 之前处于 `write fault`，会重新加载当前 FileLog 模式以便恢复写入。
- Firmware & OTA 显示当前固件大小、运行 app slot、下一 OTA slot、Max OTA upload、OTA headroom、rollback 状态，以及仅在存在错误时显示的 Last OTA error；`OTA headroom` 表示 `target slot - current sketch`，Max OTA upload 才是上传硬上限。
- 启用 Watchdog 时显示 `enabled, lifetime resets N, trip resets M` 或 invalid baseline 和 trip reset time；Reset Trip 保存时间使用和页面 NTP time 行一致的可信 epoch 判断，无可用时间则显示 `unknown (time unavailable)`。
- Partition Table 使用运行时分区表展示 Name、Type、SubType、Offset、Size、Role；Role 用于标识 running app、next OTA、app data、NVS config、OTA state、coredump 等。
- 未启用的模块不显示对应行，避免非 FULL profile 引入额外依赖。

页面风格：

- 单栏设备控制台布局，正文、顶部导航和底部系统入口使用同一页面宽度。
- 默认正文 14px；顶部主导航 14px；页面标题/内容标题约 18px/16px/15px；底部低频系统入口 12px/24px 高度，右侧运行摘要 11px，采用主次清晰的设备控制台尺度。
- 默认页面最大宽度约 1180px，浅灰页面背景配白色轻量边框 panel，避免正文和操作块错位。
- 顶部导航使用中性色和主色浅底 active 状态，主色由 CSS 变量控制。
- 内置状态、WiFi、Auth、OTA、System Logs、App Events、System 页面使用统一标题、panel、按钮和表单节奏；页面标题、正文 panel、导航和 footer 使用同一布局宽度。
- 内置页的 `POST -> 303 -> GET` 操作结果提示统一紧跟页面标题，先反馈结果，再进入具体内容块。
- 默认输入框样式只作用于文本类输入，例如未声明 type 的 input、text、password、number、email、url、tel、search。
- checkbox、radio、file、range、color、hidden 等非文本控件保持浏览器原生尺寸和行为，业务页面不需要额外覆盖基础 CSS。
- 统一 `.ok`、`.err`、`.info` 状态样式。
- 危险操作统一二次确认。
- POST 成功优先 303 redirect。
- 页面输出优先 chunked/streaming，避免整页 `String` 拼接。

推荐 helper：

```cpp
Esp32BaseWeb::sendHeader("Title");
Esp32BaseWeb::sendChunk("<p>");
Esp32BaseWeb::writeHtmlEscaped(text);
Esp32BaseWeb::sendFooter();
```

通用 chunked 响应：

```cpp
void handleCsvApi() {
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

- `beginResponse(code, contentType, filename)` 可输出 CSV、JSONL、HTML 片段或二进制下载等自定义响应；文本用 `sendChunk()`，二进制块用 `sendBytes()`。
- `contentType` 必须非空且不超过 63 字节；`filename` 仅允许字母、数字、`.`、`_`、`-`，非法时返回 false。
- 需要给业务 CSS/JS/下载资源设置缓存、nosniff 等响应头时，先调用 `sendResponseHeader(name, value)`，再调用 `beginResponse()` 或 `sendJson()` 等实际发送函数。
- handler 外调用 begin/end chunked 响应会安全失败或 no-op，并记录 WARN。
- 长响应发送会在每次实际写入客户端后主动 `yield()`；客户端已断开时停止继续输出，避免大 HTML/CSS 页面长期占住 Web 处理。
- JSON 可继续使用 `sendJson()` 一次性输出，或使用 `beginJson()` / `sendChunk()` / `writeJsonEscaped()` / `endJson()` 分块输出。

业务入口：

```cpp
Esp32BaseWeb::addPage("/fan", "Fan", handleFanPage);
Esp32BaseWeb::addPage("/config", "Config", handleConfigPage);
```

`addPage()` 注册的业务页面进入业务导航；应用必须显式提供短标题。`addApi()` 和低层 `addRoute()` 不进入业务入口列表。

业务优先导航：

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

- `HOME_ESP32BASE`：默认模式，`/` 跳转 `/esp32base`，基础库首页可列出业务入口。
- `HOME_APP`：`/` 进入业务首页，基础功能通过系统导航入口访问；`/esp32base` 保持系统入口可访问。
- `HOME_COMBINED`：`/` 进入业务首页，`/esp32base` 使用同一套导航框架展示设备融合首页。
- 业务模式默认优先使用 `/index` 作为业务首页；未显式调用 `setHomePath()` 且已注册 `/index` 页面时，`/` 会跳转 `/index`。
- 如果应用显式 `setHomePath("/")` 并注册根路径 GET 业务页面，`/` 会直接调用该业务 handler，不再产生自跳转；`/esp32base` 的系统入口行为不变。
- 配置业务首页后，导航品牌链接指向业务首页，业务主导航不会重复显示同一个首页入口。
- `SYSTEM_NAV_SECTION` 会在页面底部以小字系统入口与 `Free heap`、`Up`、`RSSI` 同行展示；窄屏下系统入口和状态摘要可自然换行，避免遮挡和横向滚动。
- Footer bar 可在 System 页面运行时切换 Off、Status only、Links + status；关闭底部横条时系统页面仍可通过直达 URL 访问。
- 基础库页面复用同一套导航框架，业务页和系统页保持一致入口结构。
- `/esp32base` 系统页按 Device、Network、Runtime Health、Storage & Logs、Firmware & OTA、Hardware、Partition Table 展示设备调试信息，包括 STA/AP/eFuse MAC、OTA headroom、last reset/wake 和运行时分区表。
- `/esp32base/api/status` 保留 `resetReason` / `wakeReason` 原始字段，并提供 `resetReasonText` / `wakeReasonText` 中文说明字段；`wifi.rssi` 返回当前 WiFi RSSI，未连接时为 `0`。
- `/esp32base/api/hostname` 返回 `currentHostname`、`defaultHostname`、`storedHostname`、`storedValid`、`restartRequired` 和校验规则；POST 参数 `hostname` 必须符合 1-32 位小写字母、数字和短横线规则，不能首尾短横线，不能包含 `.local`。

同一路径 API 分流：

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
        // save posted config
        Esp32BaseWeb::redirectSeeOther("/config?saved=1");
        return;
    }
    Esp32BaseWeb::sendText(405, "method not allowed");
}

Esp32BaseWeb::addRoute("/api/faucet/config", Esp32BaseWeb::METHOD_ANY, handleConfigApi);
```

## 8. Web UI baseline helper

业务页面继续使用 `Esp32BaseWeb::addPage()` 注册页面，并使用 `sendHeader()` / `sendFooter()` 输出基础壳层。

为保持页面结构和样式统一，业务页面优先使用：

- `sendPageTitle(title, subtitle)`：页面标题区。
- `beginPanel(title)` / `endPanel()`：内容分组。
- `sendNotice(tone, title, message)`：成功、警告、危险、信息和普通提示。
- `sendResultNotice(notices, count)`：`POST -> 303 -> GET` 后的结果提示。
- `beginMetricGrid()` / `sendMetric()` / `endMetricGrid()`：状态和统计摘要。
- `sendInfoRowCompact(title, help, value)`：只读配置、操作、向导和诊断行。
- `sendInfoRowCompactLink(title, help, value, href, label, tone)`：带按钮型链接动作的紧凑行。
- `sendInfoRowCompactForm(title, help, value, action, label, hiddenName, hiddenValue, tone)`：带单按钮 POST 动作的紧凑行，默认包含 `once()` 防重复点击。
- `sendInfoRowInlineEdit(id, title, help, value, action, inputName, inputValue, label, tone)`：单字段行内编辑，支持 AJAX 局部保存和普通 POST fallback。
- `sendInfoRowDialogForm(dialogId, targetId, title, help, value, action, fieldsHtml, label, tone)`：小表单弹层入口，适合 1-3 个字段；`fieldsHtml` 只传业务侧可信的静态表单片段，动态内容必须先 escape。
- `isAjaxRequest()`、`sendAjaxReplace()`、`sendAjaxError()`：配合 `X-Esp32Base-Ajax: 1` 处理局部提交结果。
- `sendPagination(pagination)`：页码型列表分页，包含总条数、总页数、首页、上一页、下一页、尾页、当前页附近页码、每页条数和跳页提交；`perPage=0` 时默认 10 条，每页选项为 10、15、20、30、50。

如果业务页面已经使用浏览器原生 `<dialog>`，推荐写作 `<dialog class="panel eb-modal">`。基础 `/esp32base/ui.css` 会统一原生 dialog、遮罩、`dialog.panel` 和 `dialog.eb-modal` 的轻量弹层外观；弹层内部继续复用 `fieldgrid`、`field`、`actions`、`btnlink` 和 `secondary`，无需在业务库复制 modal 样式。只读详情弹层可以增加 `data-eb-light-dismiss="1"`，允许点击遮罩关闭；危险确认、提交表单和可能丢失输入的弹层不要启用。

示例：

```cpp
void handleBusinessPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("业务配置");
    Esp32BaseWeb::sendPageTitle("配置编辑模板", "简单字段行内改，多字段对象进入独立编辑页。");
    Esp32BaseWeb::beginPanel("紧凑配置列表");
    Esp32BaseWeb::sendInfoRowCompactLink("第 1 路名称", "用于页面和记录展示，不影响实际控制。", "花坛",
                                         "/config/name", "展开修改");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}
```

样式和页面能力优先在 `examples/web_ui_gallery` 中验证；`examples/full_demo` 用于验证 Web、App Config、OTA、日志等能力在完整示例中的集成效果。

需要行内动作时优先使用 `sendInfoRowCompactLink()` 或 `sendInfoRowCompactForm()`；需要修改单个字段时使用 `sendInfoRowInlineEdit()`；需要 1-3 个字段的小表单时使用 `sendInfoRowDialogForm()`。如果业务确实需要自定义 HTML，使用底层 `sendChunk()` 手动输出，并对来自配置、URL 参数、设备名、日志、用户输入或远端数据的内容使用 `writeHtmlEscaped()`。

表单和命令提交默认仍应保留 `POST -> 303 -> GET`。行内编辑和小表单弹层可以在同一个 endpoint 中通过 `isAjaxRequest()` 返回 JSON 局部结果：成功用 `sendAjaxReplace()` 替换目标片段，失败用 `sendAjaxError()` 在当前行或弹层内显示错误。前端局部刷新只改善体验，服务端仍必须重新校验认证、同源、参数、状态和权限。

不默认局部刷新的操作包括重启、格式化、清日志、OTA、文件上传/下载/删除和 Auth 修改；这些操作应保留明确确认、进度页或整页结果，避免用户误判设备状态。

### 8.1 Skinning

Web UI baseline 支持 CSS 变量级换肤。业务项目可通过 `setHeadExtraCallback()` 覆盖少量变量：

```cpp
void handleHeadExtra() {
    Esp32BaseWeb::sendChunk("<style>:root{--eb-primary:#245f9e;--eb-primary-hover:#1f4f83;--eb-primary-soft:#e8f0f8;--eb-button-soft:#f7fafb;--eb-button-border:#d9e5ea}</style>");
}
```

不应替换语义色含义。成功、警告、危险和信息色必须保持可识别。

### 8.2 视觉检查清单

- PC 宽屏下控件不无意义拉满。
- 手机端导航、行、分页自然堆叠或横向滚动，不出现文字重叠。
- 按钮文字居中，动态内容不改变整体布局。
- 主按钮只用于明确提交/执行；普通入口、分页和低频工具动作使用浅底描边按钮，整体高度不要显得过高或过胖。
- 紧凑行动作和分页跳转使用按钮型链接，不使用小尺寸状态标签代替可点击控件。
- 紧凑行同时包含状态值和动作时，状态值与按钮之间必须有清晰间距。
- 紧凑行右侧使用固定值列和动作列；同一组内的状态值、数量和按钮应竖向对齐。
- 同组只读状态值应和带动作行的状态值位置保持一致，避免占用动作按钮视觉区域。
- 简单字段行内编辑和多字段独立编辑页有明显区别。
- 分页显示总条数、总页数、首页、上一页、下一页、尾页和当前页。
- 分页页脚必须提供当前页附近页码、每页条数选择和跳页提交；筛选条件翻页和跳页时保持不丢失；分页控件应比主操作更轻、更矮、颜色更中性，且不重复输出“当前第 N 页”。
- 筛选控件已经展示当前条件时，不重复输出一行筛选摘要标签。
- 预设时间范围不展示开始时间和结束时间；只有自定义时间范围才展示起止时间控件。
- 多字段表单使用 `editform` 控制编辑区最大宽度，并使用 `fieldgrid`、`field short`、`field med`、`field long` 或 `field full` 控制字段宽度，避免短字段被拉满。
- 重定向后的成功、失败、拒绝状态清楚可见。
- 登录、权限不足和只读受限状态可理解。
- 诊断维护页中的原始片段有长度边界，不成为主页面体验。
- 覆盖 `--eb-primary`、`--eb-primary-hover`、`--eb-button-soft` 和 `--eb-bg` 后页面仍保持语义清楚。

## 9. JSON 输出

必须 escape：

- `"`
- `\`
- `\n`
- `\r`
- `\t`
- 控制字符 `< 0x20`

不能直接拼接用户或设备字符串：

- hostname
- firmware name
- firmware version
- SSID
- error
- file content

推荐 API：

```cpp
Esp32BaseWeb::beginJson(200);
Esp32BaseWeb::sendChunk("\"name\":\"");
Esp32BaseWeb::writeJsonEscaped(text);
Esp32BaseWeb::sendChunk("\"");
Esp32BaseWeb::endJson();
```

`beginJson()` / `endJson()` 输出最外层 `{}`，中间使用 `sendChunk()` 拼接结构、`writeJsonEscaped()` 输出字符串值。

## 10. WiFi 配网状态机

进入 AP/config portal 的条件：

- 没有已保存 WiFi 凭证。
- 应用显式调用 `startConfigPortal()`。
- 用户显式清除凭证并在应用策略中选择进入 portal。
- STA 安全启动保护触发：有保存凭据的 STA 启动连续发生 guarded brownout/panic/watchdog 复位，达到 `ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS` 后暂停凭据并自动回退 portal；后续正常 `poweron`、外部复位或其他非危险复位会自动恢复一次已保存 STA 尝试。

不进入 AP/config portal、而是保持无 WiFi 诊断状态的条件：

- WiFi 初始化安全启动保护触发：设备连续在 Arduino WiFi 初始化最早期发生 guarded brownout/panic/watchdog/software reset，达到阈值后本轮跳过所有 WiFi 初始化调用。此时串口日志会输出 `wifi_init_safe_boot_pause` 和中文供电风险提示；因为 WiFi 被跳过，Web 配网页不会启动，需要通过正常上电恢复重试或修复供电后重启。

默认 config portal AP SSID 为 `ESP32-Config-XXXX`，其中 `XXXX` 取 eFuse MAC 按常见网络 MAC 顺序显示时的最后两个字节，便于和 Status 页 MAC 信息对照。

有已保存凭证但连接失败时：

- 普通连接失败不自动进入 AP/config portal；只有 STA 安全启动保护触发时才自动回退。
- 保存的 SSID 为空时视为无有效凭证；密码允许为空，用于开放 WiFi。
- 持续 STA 重连。
- 单次 STA 连接尝试有非阻塞超时，默认 `ESP32BASE_WIFI_CONNECT_TIMEOUT_MS=15000`。
- 前几次短间隔重连，连续失败后进入长间隔 backoff。
- backoff 必须非阻塞，不影响 `Esp32Base::handle()` 和 sleep 决策。

提交新凭证成功后：

- 同步保存凭证；保存失败则返回错误，不切换连接。
- 清除 STA 安全启动保护的 pause、guard 和计数。
- 停止 config portal。
- 切换到 STA 连接流程。

恢复并重试已保存 WiFi 时：

- 不修改 `ssid/pass`。
- 清除 STA 安全启动保护的 pause、guard 和计数。
- 停止 config portal。
- 用已保存凭据切换到 STA 连接流程。

进入 deep sleep 后：

- STA、AP、DNS、Web 全部不可访问。
- 唤醒后重新执行 begin/handle 流程。

## 11. Handler 性能边界

Arduino `WebServer` 是同步模型。

自定义 handler 建议：

- 单次执行 < 200ms。
- 不在 handler 中做长时间阻塞 IO。
- 长任务采用“启动任务 -> 返回 task id -> 轮询状态”的方式。
- Web handle 对超过 250ms 的慢请求输出日志；GET 使用 DEBUG，POST 等操作使用 INFO。

这属于已知限制，文档和示例必须体现。

内部发送路径要点：

- 基础库不对每个 HTTP 请求强制下发 `setNoDelay(true)`；实机回归显示该设置在弱链路下会降低 512 B chunked 响应吞吐。
- `sendChunk()` / `sendProgmem()` 共用 512 B 静态 chunk buffer；响应头仍由 Arduino `WebServer` 生成，正文 data chunk 由基础库无堆分配 writer 写出标准 chunked 帧，避免每个 chunk 触发 `WebServer::sendContent()` 内部 `malloc/free`，同时避免 PROGMEM CSS/JS 被拆成大量 128 B 小 chunk。
- `sendResponseContent()` 只在发送前检查客户端是否已经断开，发送后不再用 `client().connected()` 判定失败；`endResponse()` 在响应未标记断开时总是尝试发送最终 0-length chunk，避免同步 WebServer 下 curl/浏览器等待 chunked 响应结束直到超时。
- 长 HTML/JSON/CSV 响应在每个 data chunk 前后喂 watchdog；业务长页面仍处在同步 `WebServer::handleClient()` 内时，不再只依赖 `Esp32Base::handle()` 返回后的统一喂狗。
- 小 JSON 如果已经完整生成，使用 `sendJson()` 一次性返回固定 `Content-Length`；只有需要流式拼接或响应体较大时才使用 `beginResponse()` / `sendChunk()` / `endResponse()`。
- `sendHeader()` 不再把基础 CSS 内联进每个 HTML 页面，而是引用 `/esp32base/ui.css`；该资源使用固定 `Content-Length` 和 `Cache-Control: public, max-age=86400`，减少业务页面重复下载 9KB 级样式内容。业务 `setHeadExtraCallback()` 只作用于应用页面和自定义页面，内置 `/esp32base` 页面不承载业务 CSS 字节。
- WiFi modem sleep 默认关闭，避免按 DTIM 周期唤醒；电池业务可调用 `Esp32BaseWiFi::setPowerSave(true)` 显式恢复 modem sleep。
- 内置基础 CSS 不再包含 App Config 专用样式；启用 `ESP32BASE_ENABLE_APP_CONFIG` 时 App Config 页会按需注入额外 `<style>`，其他页面不下发这部分字节。
