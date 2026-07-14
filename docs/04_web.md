# Web 与配网

## 1. 定位

Web 层用于小型局域网管理，不是大型 Web 平台。

页面结构、样式基线、业务页面模式、换肤策略和视觉调整规则见 [Web UI 页面结构与样式基线](11_web_ui_baseline.md)。

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
- 应用可在 `Esp32Base::begin()` 前调用 `Esp32BaseWeb::setDefaultAuth()` 设置默认认证。
- 认证优先级为：已保存认证 > 应用默认认证。未设置应用默认认证且没有已保存认证时，Web 服务不会启动。
- 仅显式启用 `ESP32BASE_WEB_ALLOW_INSECURE_DEFAULT_AUTH=1` 的受控开发固件会使用库内置 `admin/admin` 兜底，并输出 WARN 审计日志。
- 业务项目需要允许用户修改认证账号/密码时，优先链接内置 `/esp32base/auth` 认证管理页面。
- 业务自建配置页时，应先 `checkAuth()`，再用 `verifyAuth(currentUser, currentPass)` 验证当前凭据，最后调用 `saveAuth(newUser, newPass)`。
- 更新 Web Auth 会写入后读回校验，保存成功后立即生效；浏览器缓存旧 Basic Auth 时，后续请求会重新触发认证。保存失败不会主动清空旧认证。
- Web Auth 持久化使用 `eb_web.auth_user`、`eb_web.auth_pass`。日志可输出用户名、来源、结果和 `password_set` 状态，但不输出密码值。
- Basic Auth 请求日志每个 HTTP 请求最多输出 1 条，避免 OTA 上传分块反复校验时刷屏。
- WiFi 密码字段不会回显已保存密码。留空表示保持已保存密码；需要切换开放网络时必须显式勾选清除密码。
- WiFi 凭据保存会写入后读回校验；保存失败时不会切换运行中连接尝试。

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
- Web Auth 密码不在 HTML、JSON、API 响应或日志中输出；日志只输出用户名、来源、结果和 `password_set` 状态。当前持久化仍保存到普通 NVS，未启用平台级 flash encryption 时不应视为密文凭据库。
- 内置危险 POST 必须是 POST method，并会做轻量 `Origin` / `Referer` host 校验；存在这些头时必须与请求 `Host` 同源，缺失时放行，保证 curl、PlatformIO `webota` 和简单脚本可用。非 POST 返回 405，校验失败返回 403，不执行副作用。

## 5. 路由表

规则：

- 固定容量。
- 应用路由可在 `Esp32Base::begin()` 前注册。
- 应用路由可在 Web 启动前注册。
- Web 已启动后注册的路由应立即生效。
- route path 必须以 `/` 开头，最大可见长度为 47 字节。
- 应用静态资源使用独立固定容量表，不消耗应用 route 容量；默认容量为 `ESP32BASE_WEB_MAX_STATIC_ASSETS=8`。

机制：

- `Esp32BaseWeb::addRoute()` 写入静态路由表。
- Web 启动时把缓存路由注册到 `WebServer`。
- Web 已 ready 时，新增路由立即注册。
- `Esp32BaseWeb::addStaticAsset()` 注册应用 CSS/JS/图片等固件内固定资源；资源通过基础库 `WebServer` 输出固定 `Content-Length`、`X-Content-Type-Options: nosniff` 和 `Cache-Control`，默认要求 Web Basic Auth，业务资源不能注册到 `/esp32base/**` 内置命名空间，业务不应绕开 WebServer 或复制静态资源发送逻辑。
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
- 业务页面可优先复用 `docs/11_web_ui_baseline.md` 中定义的页面能力、helper 和基础 CSS。现有能力不适合时，业务侧可以做最小局部实现；只有多个项目确实会复用的模式，才回到 Esp32Base 补通用 helper、样式、示例和文档。

默认容量：

```cpp
#define ESP32BASE_WEB_MAX_ROUTES 24
```

该默认值覆盖常见业务页面/API 增长，不要求业务项目一开始就为 route 表单独调参。route 较少且需要节省静态 RAM 的应用，可以在构建参数中显式调小 `ESP32BASE_WEB_MAX_ROUTES`；页面/API 继续增长时也可以按项目显式调大。

静态资源注册示例：

```cpp
static const uint8_t APP_CSS[] PROGMEM = "body{--brand:#176b7a}";

Esp32BaseWeb::addStaticAsset("/assets/app.css",
                             "text/css; charset=utf-8",
                             APP_CSS,
                             sizeof(APP_CSS) - 1,
                             86400,
                             true);
```

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

- `GET /esp32base/app-events`，独立应用业务事件页面，不混入系统诊断日志；支持 `page/per/level/time/eventCode/reasonCode` 筛选参数，非法筛选返回 `400 invalid_filter`。
- `GET /esp32base/api/app-events?offset=0&limit=50`，按最新优先分页输出 JSON；支持 `level/time/eventCode/reasonCode` 筛选参数，非法筛选返回 `400 {"ok":false,"error":"invalid_filter"}`。
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
- `POST /esp32base/tools/business-records-clear`，仅 `ESP32BASE_ENABLE_RECORD_STORE=1`，逻辑清空所有通过 `registerBusinessRecordStore()` 登记的当前版本业务 Store；需要认证、同源 POST 和用户二次确认
- `POST /esp32base/tools/app-events-clear`，仅 `ESP32BASE_ENABLE_APP_EVENTS=1`，清空事件日志；System 页面危险操作入口，成功后回到 System 页面
- `POST /esp32base/tools/watchdog-trip-reset`
- `POST /esp32base/tools/format-fs`，仅 `ESP32BASE_ENABLE_FS=1`，只格式化 LittleFS；格式化成功后通知 after-format 回调/事件，不清任何 NVS
- `POST /esp32base/api/restart`

OTA 路由只在启用 OTA 且认证条件满足时注册。

## 7. 内置页面交互

WiFi 配置页：

- 回显当前 SSID。
- 不回显当前密码；留空表示保持已保存密码，显式勾选清除密码才会切换开放网络。
- 页面应清楚区分 WiFi 保存和清除凭据这类危险操作。
- 表单提交前用前端 JS 校验 SSID 非空；密码可为空，用于开放 WiFi。
- 服务端 API 必须重复校验 SSID 非空，密码允许为空。
- 空 SSID 返回错误，不保存凭证。
- SSID 超过 32 字节或密码超过 64 字节时返回错误，不静默截断。
- `/esp32base/wifi` 和 `/esp32base/api/wifi` 都是表单提交端点，成功/失败使用 303 跳转到 HTML 页面；它们不是 JSON API。
- 当已保存凭据因 STA 安全启动保护暂停时，页面显示恢复说明和 guarded reset 计数，并提供“恢复并重试已保存 WiFi”操作；该操作调用 `/esp32base/api/wifi/retry`，不要求用户重新保存同一组密码。

System 维护页：

- 默认底部系统导航展示 Status、System Logs、System；`System Logs` 对应 `/esp32base/logs`，是系统诊断日志查看入口，底层实现仍为 `Esp32BaseFileLog`。启用 App Events 时额外展示 App Events 直达入口，启用 App Config 时额外展示 App Config 直达入口。WiFi、Auth、OTA 是低频配置/维护入口，收在 System 页面中并显示为 WiFi Setup、Web Auth、Firmware OTA，但保留原直达 URL。
- System 页面应把低频入口、设置项、普通维护项和危险操作分区展示，保证桌面端和手机端都能清楚区分说明、当前状态和操作入口。
- 启用 App Config 时，System 页面首位仍显示 App Config 入口；App Config 是业务持久化参数配置页，不和基础库维护参数混在 System 长页面中。
- Hostname 设置区显示当前 hostname、构建默认 hostname、已保存 hostname 和是否需要重启；保存只写入 `eb_sys.hostname`，不热切换当前 DHCP hostname、mDNS、OTA 或 Web 身份，页面必须提示重启后生效。
- Footer bar 模式设置只接受 Off、Status only、Links + status，保存后立即生效并写入 `eb_ui.footer_mode`；该设置只控制底部横条，不关闭直达 URL 或顶部业务导航。
- 重启按钮必须有二次确认，并通过统一 lifecycle restart 执行；POST 响应必须替换浏览器历史到 GET URL，避免刷新重复提交。
- `/esp32base/api/restart` 是脚本兼容入口，返回纯文本并立即进入重启流程，不提供 JSON 错误模型。
- 重启和格式化等危险操作必须分组显示，避免按钮与下一项标题贴得太近。
- 启用 Watchdog 的 profile 显示 Watchdog lifetime/trip 小计维护；当 `wdt_trip_base` 大于 lifetime 时显示 `invalid baseline`，Reset Watchdog Trip 写入并回读确认 `eb_sys.wdt_trip_base` 和 `eb_sys.wdt_trip_time`，不清 `eb_sys.wdt_cnt`。
- 启用 FS 的 profile 显示 `Format LittleFS`，该操作会删除日志和所有 LittleFS 文件，但不清除 WiFi、Web Auth、业务 namespace 或任何 NVS 配置。
- 启用 FileLog 的 profile 显示系统诊断日志模式设置、运行态和 `Clear system logs`；模式设置只接受 OFF、ERROR、WARN、INFO，保存后立即生效并写入 `eb_log.mode`，清空日志只接受 POST，成功后回到 System 页面显示结果。运行态为 `write fault` 时表示配置模式仍开启，但 FileLog 因 FS 写入故障被运行期保护停写；已有日志仍可能可读，不应显示成 `disabled`。运行态为 `unavailable` 时表示配置模式开启但 FS/init 前置条件未满足。运行态为 `disabled` 时明确 FileLog 模式为 OFF，新日志不会写入。
- 系统诊断日志默认文件为 `/esp32base/logs/system.log`，App Events store 默认目录为 `/esp32base/records/app-events.v1/`；二者都位于 `/esp32base/**` 基础库管理命名空间。
- 启用 RecordStore 且业务通过 `registerBusinessRecordStore()` 登记当前 Store 后，System 页显示独立的 `Clear Business Records` 红色危险操作框，列出每个 Store 的版本目录、状态和记录数。清空前必须先确认所有 Store 可操作；预检失败时零修改，执行中失败时停止并报告已清空数量。成功后原 Store 对象保持可用、记录数为0、ID继续递增。该操作不扫描 `/esp32base/records/**`，不清 App Events，也不处理未登记历史版本。
- 启用 App Events 的 profile 显示 `Clear App Events` 危险操作；它只对 App Events store 做逻辑清空，不重写数据区，也不清系统诊断日志、WiFi、Web Auth、NVS 配置或其他 LittleFS 文件。
- 格式化 FS 是显式 POST 操作；执行前 flush 系统诊断日志，格式化成功后重新 mount FS、重新加载 FileLog 模式、重新创建 App Events，并逐个 `reload()` 已登记的当前业务 Store；任一 Store 恢复失败都不能提示整体成功。只有 `Esp32BaseFs::format()` 成功时才触发 `Esp32BaseWeb::setAfterFormatFsCallback()` 注册的回调和 `EVENT_TOOLS_FORMAT_FS_SUCCESS` 事件；格式化失败、启动挂载失败和其他 FS 维护动作不触发。回调结果除原字段外还包含 `businessRecordStoreCount`、`businessRecordStoreReloadedCount` 和 `businessRecordStoresReloadSuccess`；业务仍负责在回调/事件里同步自己的派生统计、文件索引或缓存。重启请求同样输出 WARN 级维护日志。
- 普通 `GET` 页面慢请求只输出 DEBUG；`POST` 等操作慢请求输出 INFO，默认 ERROR 系统日志不记录，现场排查时可切到 INFO 查看。
- API 层仍建议要求 POST，不使用 GET 触发重启。
- 内置危险 POST 包括 WiFi 保存/清除、App Config 保存、Hostname 保存、Auth 保存、重启、System 操作、System Logs clear、Business Records clear、App Events clear、Web OTA upload/done；跨站 `Origin` 或 `Referer` 会被拒绝。
- 业务自定义 POST 或危险操作应优先调用 `Esp32BaseWeb::checkPostAllowed(context)`，不要只做 `checkAuth()`；即使业务误把危险 handler 注册为 `METHOD_ANY`，该 helper 也会拒绝非 POST 请求。

Status 页：

- `/esp32base` 默认作为只读设备体检页，不承载配置保存和危险操作。
- 第一屏不再额外做 `System Overview` 预览块，避免和相邻详细分区重复；常用信息直接按 Device、Network、Runtime Health、Storage & Logs、Firmware & OTA、Hardware 排序。
- Status 页不做 LittleFS 全量文件树扫描、文件可读性检查或 top files 统计；Storage & Logs 只显示一次 LittleFS 信息查询得到的 O(1) 容量摘要和 FileLog 配置摘要，不打开 FileLog 段文件统计已用大小，完整 FS inventory 位于 `/esp32base/fs`。
- 同一数据不应在相邻分区同名同值重复展示；需要高频查看的信息应前置为正式分区，而不是再做一层摘要。
- Partition Table 属于低频详细信息，只在 `/esp32base?details=1` 显示。
- 系统诊断日志（`Esp32BaseFileLog`）属于 Storage & Logs，不作为健康摘要项；状态页默认显示日志级别、轮转文件数、容量上限和路径，不打开段文件统计当前大小，避免慢 FS 文件打开拖慢首屏。

App Config 页面：

- 仅在 `ESP32BASE_ENABLE_APP_CONFIG=1` 时注册，固定路径为 `/esp32base/app-config`。
- 业务必须在 `Esp32Base::begin()` 前用 `Esp32BaseAppConfig` 注册 group 和字段；字段直接绑定业务 `Esp32BaseConfig` namespace/key；注册传入的字符串和 enum option 数组必须保持固件生命周期有效。
- 启用后必须显式设置 `ESP32BASE_APP_CONFIG_MAX_GROUPS` 和 `ESP32BASE_APP_CONFIG_MAX_FIELDS`，硬上限为 16 组、128 字段。
- 未注册 group 或字段时页面显示轻量空状态，提示应用尚未注册配置字段。
- 支持 string、int、decimal 定点数、bool、enum；string 最大 256 bytes，enum value 最大 31 bytes，label/option label 最大 31 bytes，help 最大 96 bytes，unit 最大 12 bytes，decimal 使用 `int32_t raw` 和 `scale=0..6`。
- 页面按 group 展示字段，字段默认直接可编辑；点击 Save 时前端只展示变更核对清单，不作为服务端事实来源。
- 变更核对清单只用于提交前确认；只有实际保存结果才作为成功或失败反馈。
- POST 保存时后端重新解析、执行内置校验、执行字段级和页面级业务校验、读取当前 NVS 并计算实际变化字段。
- 任一校验失败时零写入；校验通过后只保存变化字段，未变化字段绝不写 NVS。
- 只读状态、未来版本或业务版本不兼容等业务规则应通过页面级校验作为保存前 veto 处理；校验回调可读取本次整页提交值，拒绝时不会进入任何 App Config 字段写入。
- 校验通过后若发生 NVS 写入失败，已成功写入的字段不会回滚；`ChangeCallback` 只对成功字段触发，整次保存结束的 `SaveCallback` summary 会报告失败数量，页面返回 partial 提示。
- 保存成功字段会触发 `ChangeCallback`，回调包含旧值和新值；整次保存结束触发 `SaveCallback` summary。
- 页面带 RAM revision；旧页面提交会被拒绝并提示刷新，避免覆盖已经变化的配置。
- 字段可标记 `restartRequired`；保存后不会自动重启，但未重启会话内页面会持续提示这些字段仍待重启生效，并显示运行中旧值和已保存新值；极低内存下 string/enum 旧值可能显示为 `unavailable`，提示仍保留到重启。
- 当前版本不提供单字段弹窗、多配置页、敏感字段隐藏或 CSRF token；安全边界为 Web Auth、POST only 和 Origin/Referer 同源检查。

OTA 上传页：

- 使用 Web Basic Auth，不额外要求单独认证。
- 页面保持上传控件、SHA256 可选校验和进度反馈在同一任务流程内。
- 上传中显示进度。
- 页面进度同时显示百分比、已处理容量和总容量。
- 页面容量值只显示 KB/MB/B 人性化格式；OTA 状态 API 继续保留 raw `bytes` 数值并附带 `human` 字段。
- 上传成功后提示设备正在重启，并在短暂等待后跳转到当前配置的首页。
- 上传失败时留在 OTA 页面，显示失败原因，并允许用户重新选择固件上传。

System Logs 页面：

- 需要 Basic Auth。
- FS/FileLog 不可用时显示轻量不可用面板，并说明不可用原因。
- Clear system logs POST 后通过 303 回到 System Logs 页，并在页面顶部显示成功或失败提示；刷新页面不重复提交。
- FileLog 模式为 OFF 时，System Logs 页面仍展示已有历史日志；OFF 只表示停止后续写入。
- `GET /esp32base/logs` 和 `GET /esp32base/logs/raw` 必须保持只读，只读取已经落盘的系统诊断日志快照，不主动 `flush()`、创建、清空或重建文件。页面会展示当前 buffer used/total，尚在缓存中的 INFO 日志可等待常规 flush interval；清空、格式化、重启等维护副作用仍必须通过 POST，不通过 GET 触发。
- System Logs 页面只面向 Esp32Base 系统诊断日志，不读取 `Esp32BaseAppEvents`，也不展示业务事件。页面命名面向用户，`Esp32BaseFileLog` 是底层实现/API 名称。
- 显示系统诊断日志的启用状态、路径、模式、轮转、缓冲区和容量摘要；容量值用 KB/MB/B 人性化格式展示，不重复暴露 raw bytes。
- 页面提供 current 和历史 segment 的切换入口，并展示各 segment 的文件名和大小。
- 默认显示 `current-0`；可通过 `?segment=N` 查看单个历史文件，非法或越界 segment 回落到 `current-0`。
- 日志正文不内联进主 HTML；System Logs 页面通过 iframe 加载 `/esp32base/logs/raw?segment=N` 的 `text/plain` 原文，避免大日志逐字符 HTML escape。
- 清空日志入口位于 System 页面；System Logs 页面只负责查看日志。
- 日志正文一次只展示一个 segment，包括 0 字节文件；非法或越界 segment 回落到 `current-0`。
- raw 日志内容不折叠、不省略、不规整空白行，页面展示应忠实反映文件内容。
- 日志正文流式输出期间必须周期性 yield/feed Watchdog，避免大日志页面长请求触发看门狗。

App Events 页面：

- 仅在 `ESP32BASE_ENABLE_APP_EVENTS=1` 时注册，标题和导航标签为 `App Events`，可通过 `setBuiltinLabel(BUILTIN_APP_EVENTS, "...")` 覆盖。
- 这是App Events的数值诊断视图，不属于System Logs，也不注册业务代码到显示文字的映射。
- 页面以分区清楚的状态摘要、筛选区和最新优先分页列表展示容量、文件大小、有效记录数、损坏记录数、存储路径和事件内容，并提供CSV导出。
- 筛选条件包括等级、时间类型、`eventCode` 和 `reasonCode`；HTML、JSON API、CSV共用同一组数值筛选语义。
- 列表紧凑展示ID、完成时间及相对时间、等级、事件码、原因码、对象ID、两个value和flags；每条事件提供详情入口，按Identity & level、Timing、Event fields、Values & flags分组展示全部公开字段。CRC损坏或不完整记录绝不展示，只在存储状态中报告数量和错误；CRC、slot、magic和commit等内部存储字段不属于事件详情。
- 有可信epoch或本次boot可解析时显示真实完成时间；否则显示 `boot N uptime N s`，不伪造日期。
- 清空事件日志是危险操作，只在 System 页面提供 `POST /esp32base/tools/app-events-clear`，必须通过 Web Auth 和同源检查，成功后 303 回到 System 页面。
- JSON事件包含通用记录时间、`eventCode/reasonCode/objectId/value1/value2/flags/level`，并给出可解析的开始和完成epoch；0表示无法解析。
- JSON API只输出CRC有效的事件。业务页面负责把数字代码和flags映射为业务语言。
- CSV导出同样只包含有效语义字段，不输出slot、magic、CRC或commit等内部布局。
- 该页面明确和 `/esp32base/logs` 分离：`/esp32base/logs` 是 System Logs，展示 `Esp32BaseFileLog` 系统诊断日志，记录启动、联网、OTA、基础库运行等系统信息；`/esp32base/app-events` 是应用业务事件日志，记录应用显式写入的业务事件。
- 应用页面不应把 boot/reset、WiFi、NTP、OTA、LittleFS、FileLog fault 或基础库健康状态重复写入 App Events；这些系统诊断信息继续由 Status、System diagnostics 和 FileLog 展示。同一故障如果同时影响业务，App Events 应只表达业务影响、保护动作、跳过原因、用户维护结果或外部决策结果。App Events 页面只用于查看应用因业务原因写入的事件。

状态页/API：

- 内置系统首页的 heap、flash、FS、FileLog 容量只显示 KB/MB/B 人性化格式，不重复 raw bytes。
- OTA 页面进度只显示 KB/MB/B 人性化格式；API 大数字字节数可继续包含 raw `bytes` 和 `human`。
- JSON 字段建议同时提供 `bytes` 和 `human`，避免前端重复实现单位转换。

系统首页：

- `/esp32base` 是只读设备体检页，采用诊断优先结构：默认按 Device、Network、Runtime Health、Storage & Logs、Firmware & OTA、Hardware 展示调试信息，不额外显示相邻重复的总览预览块；低频分区表和运行 ELF SHA 位于 `/esp32base?details=1`。
- Status 页展示设备身份、网络、运行健康、存储与日志、固件 OTA 和硬件摘要。多值信息使用紧凑子指标，避免逗号串联造成阅读困难。
- Status 页不打开日志段文件、不扫描完整文件树、不统计 Top 文件列表；低频或较重的文件细节放在 `/esp32base/fs`，运行 ELF SHA 和分区表放在 `/esp32base?details=1`。
- `/esp32base/fs` 默认是只读 LittleFS 详情页，展示容量摘要、主要文件和文件树。文件树应区分普通文件、不可读文件和基础库管理文件；不可读文件不能给出会生成 0 字节伪成功的下载入口。当 FS used 明显大于可见文件合计时，应提示可能存在内部、历史或不可见占用。业务侧如果使用 `Esp32BaseFs` 读写文件，也必须检查返回值；逻辑大小存在不代表内容块一定可读。
- `/esp32base/fs?manage=1` 增加单文件删除和受限上传，不提供目录删除、批量删除、编辑、重命名、移动或任意路径输入；删除必须通过 `POST /esp32base/fs/delete -> 303 -> GET`，格式化仍只在 System 页危险操作区。上传保留本地文件名，只写入已有目录；覆盖上传必须显式确认，并避免上传中断时先清空旧文件。基础库管理文件应在文件树中给出维护提醒；维护操作修改 FileLog 或 App Events store 后，应刷新对应运行态。
- Firmware & OTA 默认显示运行 app slot、下一 OTA slot、Max OTA upload、rollback 状态，以及仅在存在错误时显示的 Last OTA error；默认状态页不调用当前镜像 size 校验，也不计算 `OTA headroom`。Max OTA upload 是下一 OTA slot 的上传硬上限。
- 启用 Watchdog 时显示 `enabled, lifetime resets N, trip resets M` 或 invalid baseline 和 trip reset time；Reset Trip 保存时间使用和页面 Time 行一致的可信 epoch 判断，无可用时间则显示 `unknown (time unavailable)`。
- 启用 Time 时，Status 页显示统一 `Time` 行，包含当前来源 `uptime/rtc/ntp`、可信状态、当前时间和 uptime；启用 RTC 时额外显示 `RTC` 行，包含驱动、状态、最近读取 epoch 和读取时的 uptime；启用 NTP 时显示 `NTP` 行，只表示联网对时客户端自身状态。
- `/esp32base?details=1` 使用运行时分区表展示 Name、Type、SubType、Offset、Size、Role；Role 用于标识 running app、next OTA、app data、NVS config、OTA state、coredump 等。
- 未启用的模块不显示对应行，避免非 FULL profile 引入额外依赖。

页面风格：

- 单栏设备控制台布局，正文、顶部导航和底部系统入口使用同一页面宽度。
- 字号和控件尺寸采用主次清晰的设备控制台尺度，避免页面在桌面端显得松散、在手机端难以点击。
- 页面宽度、背景和内容块样式保持统一，避免正文和操作块错位。
- 顶部导航使用中性色和主色浅底 active 状态，主色由 CSS 变量控制。
- 内置状态、WiFi、Auth、OTA、System Logs、App Events、System 页面使用统一标题、内容块、按钮和表单节奏；页面标题、正文、导航和 footer 使用同一布局宽度。
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
- 固件内固定 CSS/JS/图片优先使用 `addStaticAsset()`，让基础库统一输出固定长度和缓存头；动态下载或按需导出需要自定义头时，先调用 `sendResponseHeader(name, value)`，再调用 `beginResponse()` 或 `sendJson()` 等实际发送函数。
- handler 外调用 begin/end chunked 响应会安全失败或 no-op，并记录 WARN。
- 长响应发送会在每次实际写入客户端后主动 `yield()`；客户端已断开时停止继续输出，避免大 HTML/CSS 页面长期占住 Web 处理。
- 当前仍使用 Arduino 同步 `WebServer`，单个大 HTML/JSON/CSV 响应发送期间会占用 `handleClient()`，新连接需等待当前 handler 返回；业务长任务采用“启动任务 -> 返回 task id -> 轮询状态”，大历史数据优先分页、缩短默认 range 或异步加载。
- JSON 可继续使用 `sendJson()` 整体输出，或使用 `beginJson()` / `sendChunk()` / `writeJsonEscaped()` / `endJson()` 分块输出。

native handler 测试：

```cpp
void handleConfigApi() {
    if (!Esp32BaseWeb::checkPostAllowed("config")) {
        return;
    }
    char mode[16];
    Esp32BaseWeb::getParam("mode", mode, sizeof(mode));
    Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
}

void test_config_api_post() {
    Esp32BaseWeb::nativeTestReset();
    Esp32BaseWeb::nativeTestBeginRequest(Esp32BaseWeb::METHOD_POST, "/api/config");
    Esp32BaseWeb::nativeTestSetParam("mode", "auto");
    Esp32BaseWeb::nativeTestSetAuthenticated(true);
    Esp32BaseWeb::nativeTestSetSameOrigin(true);

    Esp32BaseWeb::nativeTestRun(handleConfigApi);

    const auto& response = Esp32BaseWeb::nativeTestResponse();
    TEST_ASSERT_EQUAL(200, response.code);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", response.body.c_str());
}
```

- native harness 只在 `ESP32BASE_WEB_NATIVE_TEST=1` 且非 ESP32 目标中可用；固件构建不会暴露这些接口。
- 测试可设置 method、path、参数、body、认证结果和同源结果，并捕获 `sendText()`、`sendHtml()`、`sendJson()`、`redirectSeeOther()`、`beginResponse()`、`sendResponseHeader()`、`sendChunk()`、`sendBytes()`、`endResponse()` 的状态码、content type、headers 和响应体。
- 它适合应用项目直接行为级测试 handler 的 method 分流、Auth/POST/同源保护、参数读取、重定向和响应内容；不替代真实 ESP32 WebServer、multipart 上传、文件系统、OTA 或浏览器集成验证。

业务入口：

```cpp
Esp32BaseWeb::addPage("/fan", "Fan", handleFanPage);
Esp32BaseWeb::addPage("/config", "Config", handleConfigPage);
```

`addPage()` 注册的业务页面进入业务导航；应用必须显式提供短标题。`addApi()` 和低层 `addRoute()` 不进入业务入口列表。

业务优先导航：

```cpp
Esp32BaseWeb::setDeviceName("Device");
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
- `/esp32base` 系统页按设备、网络、运行健康、存储日志、固件 OTA 和硬件维度展示调试信息；运行 ELF SHA 与运行时分区表只在 `/esp32base?details=1` 显示。
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

Esp32BaseWeb::addRoute("/api/device/config", Esp32BaseWeb::METHOD_ANY, handleConfigApi);
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

如果业务页面已经使用浏览器原生 `<dialog>`，可复用基础 `/esp32base/ui.css` 的轻量弹层、表单和按钮样式，无需在业务库复制 modal CSS。只读详情弹层可以增加 `data-eb-light-dismiss="1"`，允许点击遮罩关闭；危险确认、提交表单和可能丢失输入的弹层不要启用。

示例：

```cpp
void handleBusinessPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("业务配置");
    Esp32BaseWeb::sendPageTitle("配置编辑示例", "简单字段行内改，多字段对象进入独立编辑页。");
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

不默认局部刷新的操作包括重启、格式化、清日志、OTA、文件上传/下载/删除和 Auth 修改；这些操作应保留明确确认、进度页或整页结果，避免用户误判设备状态。FS 覆盖上传必须避免上传中断时先清空旧文件。

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
- 主按钮只用于明确提交/执行；普通入口、分页和低频工具动作保持较轻视觉层级，整体高度不要显得过高或过胖。
- 紧凑行动作和分页跳转使用按钮型链接，不使用小尺寸状态标签代替可点击控件。
- 紧凑行同时包含状态值和动作时，状态值与按钮之间必须有清晰间距。
- 紧凑行同时包含状态值和动作时，同一组内的状态值、数量和按钮应保持稳定对齐。
- 同组只读状态值应和带动作行的状态值位置保持一致，避免占用动作按钮视觉区域。
- 简单字段行内编辑和多字段独立编辑页有明显区别。
- 分页显示总条数、总页数、首页、上一页、下一页、尾页和当前页。
- 分页页脚必须提供当前页附近页码、每页条数选择和跳页提交；筛选条件翻页和跳页时保持不丢失；分页控件应比主操作更轻，且不重复输出“当前第 N 页”。
- 筛选控件已经展示当前条件时，不重复输出一行筛选摘要标签。
- 预设时间范围不展示开始时间和结束时间；只有自定义时间范围才展示起止时间控件。
- 多字段表单应控制编辑区最大宽度，并按字段内容复杂度控制输入宽度，避免短字段被拉满。
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

- WiFi 初始化安全启动保护触发：设备连续在 Arduino WiFi 初始化最早期发生 guarded brownout/panic/watchdog/software reset，达到阈值后本轮跳过所有 WiFi 初始化调用，并输出可诊断的供电风险提示；因为 WiFi 被跳过，Web 配网页不会启动，需要通过正常上电恢复重试或修复供电后重启。

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

发送路径边界：

- 长 HTML/JSON/CSV 响应应通过 `beginResponse()` / `sendChunk()` / `sendBytes()` / `endResponse()` 流式输出，并在发送过程中让出调度和喂 watchdog。
- 小 JSON 如果已经完整生成，可直接使用 `sendJson()` 返回。
- 客户端断开时，基础库应停止继续输出当前响应；客户端主动刷新、断网或取消下载不应进入默认 ERROR 系统诊断日志。
- `sendHeader()` 引用 `/esp32base/ui.css`，避免每个 HTML 页面重复内联基础 CSS；业务 `setHeadExtraCallback()` 只作用于应用页面和自定义页面。
- `addStaticAsset()` 适合注册固件内小型 CSS/JS/图片；应用传入的数据和 content type 字符串必须在固件生命周期内有效。
- WiFi modem sleep 默认关闭，避免按 DTIM 周期唤醒；电池业务可调用 `Esp32BaseWiFi::setPowerSave(true)` 显式恢复 modem sleep。
- 内置基础 CSS 不再包含 App Config 专用样式；启用 `ESP32BASE_ENABLE_APP_CONFIG` 时 App Config 页会按需注入额外 `<style>`，其他页面不下发这部分字节。
