# Web 与配网

## 1. 定位

Web 层用于小型局域网管理，不是大型 Web 平台。

能力：

- WebServer。
- Basic Auth。
- 状态 API。
- WiFi 配网页面/API。
- Logs 页面。
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
- 调用 `Esp32BaseWeb::setAuthEnabled(false)` 后，内置 HTTP 路由会完全开放，包括 WiFi 保存/清除、Auth 保存、重启、Tools、Logs clear 和 Web OTA；这只适合受控调试网络，不适合暴露到外部网络。
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
- 内置危险 POST 会做轻量 `Origin` / `Referer` host 校验；存在这些头时必须与请求 `Host` 同源，缺失时放行，保证 curl、PlatformIO `webota` 和简单脚本可用。校验失败返回 403，不执行副作用。

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
- `setSystemNavMode()` 控制 Status、WiFi、OTA、Logs、Tools 等基础功能入口在顶部、底部或底部紧凑系统工具区展示。
- `setBuiltinLabel()` 可覆盖 Status/WiFi/OTA/Logs/Tools/Auth 标签，用于应用统一本地化；系统工具页统一使用 `BUILTIN_TOOLS`，不提供旧 Reboot 历史别名。
- `setHeadExtraCallback()` 可在 `sendHeader()` 的 `</head>` 和顶部导航输出前注入业务 CSS，业务页面不需要复制基础库 header/nav。
- 顶部导航按当前请求路径输出 `active` class：完全匹配优先，嵌套路由按最长 path 前缀匹配；`SYSTEM_NAV_SECTION` 下系统维护入口仍只在 footer 中展示。
- 默认 Web 样式采用简洁中性色，普通链接、导航和 tabs 不使用蓝色主色；业务项目最终视觉仍由 `setHeadExtraCallback()` 注入 CSS 覆盖。

默认容量：

```cpp
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define ESP32BASE_WEB_MAX_ROUTES 12
#else
#define ESP32BASE_WEB_MAX_ROUTES 16
#endif
```

## 6. 内置路径

Web:

- `GET /esp32base`
- `GET /esp32base/api/status`
- `GET /esp32base/api/chip`
- `GET /esp32base/api/firmware`
- `GET /esp32base/wifi`
- `POST /esp32base/wifi`
- `POST /esp32base/api/wifi`
- `POST /esp32base/api/wifi/clear`
- `GET /esp32base/auth`
- `POST /esp32base/auth`

OTA:

- `GET /esp32base/ota`
- `POST /esp32base/ota`
- `GET /esp32base/api/ota`

Logs:

- `GET /esp32base/logs`
- `POST /esp32base/logs/clear`

Tools:

- `GET /esp32base/tools`
- `POST /esp32base/tools/reboot`
- `POST /esp32base/tools/format-fs`
- `POST /esp32base/api/restart`

OTA 路由只在启用 OTA 且认证条件满足时注册。

## 7. 内置页面交互

WiFi 配置页：

- 回显当前 SSID。
- 回显当前密码。
- 表单提交前用前端 JS 校验 SSID 非空、密码非空。
- 服务端 API 必须重复校验 SSID 非空、密码非空。
- 空 SSID 或空密码返回错误，不保存凭证。
- SSID 超过 32 字节或密码超过 64 字节时返回错误，不静默截断。

Tools 维护页：

- 重启按钮必须有二次确认，并通过统一 lifecycle restart 执行；POST 响应必须替换浏览器历史到 GET URL，避免刷新重复提交。
- 重启和格式化等危险操作必须分组显示，避免按钮与下一项标题贴得太近。
- 启用 FS 的 profile 显示 `Format LittleFS`，该操作会删除日志和所有 LittleFS 文件，但不清除 WiFi、Web Auth 或 NVS 配置。
- 启用 FileLog 的 profile 显示 `Clear logs`，清空日志只接受 POST，表单使用 `confirm()` 和 `once(form)`，成功后回到 Tools 页面显示结果。
- 格式化 FS 是显式 POST 操作；执行前 flush 文件日志，成功后重新 mount FS，并重新加载文件日志配置；成功提示应明确显示在 Tools 页面，同时输出 WARN 级维护日志记录请求、format/mount/FileLog reload 结果。重启请求同样输出 WARN 级维护日志。
- 普通 `GET` 页面慢请求只输出 DEBUG；`POST` 等操作慢请求继续输出 WARN。
- API 层仍建议要求 POST，不使用 GET 触发重启。
- 内置危险 POST 包括 WiFi 保存/清除、Auth 保存、重启、Tools 操作、Logs clear、Web OTA upload/done；跨站 `Origin` 或 `Referer` 会被拒绝。

OTA 上传页：

- 使用 Web Basic Auth，不额外要求单独认证。
- 上传中显示进度。
- 进度同时显示百分比、已处理字节数和总字节数。
- 字节数必须同时给出 raw bytes 和 KB/MB 人性化格式。
- 上传成功后提示设备正在重启，并在短暂等待后跳转到当前配置的首页。
- 上传失败时留在 OTA 页面，显示失败原因，并允许用户重新选择固件上传。

Logs 页面：

- 需要 Basic Auth。
- FS/FileLog 不可用时显示 `File log: unavailable`。
- 读取日志内容前必须调用 `Esp32BaseFileLog::flush()`。
- 显示 enabled、path、file level、rotate files、buffer used/total、flush interval、max per file、max total、每段大小。
- 文件日志状态信息使用紧凑小字号展示；其中的容量值只显示 KB/MB/B 人性化值，不重复 raw bytes。
- 页面顶部以标签展示所有 segment，顺序为 `current-0`、`history-1`、`history-2` 到最旧 history；标签里的大小只显示 KB/MB/B 人性化值，不重复 raw bytes。
- 默认显示 `current-0`；可通过 `?segment=N` 查看单个历史文件，非法或越界 segment 回落到 `current-0`。
- 日志内容必须 HTML escape。
- 清空日志入口位于 Tools 页面；Logs 页面只负责查看日志。
- 日志正文一次只展示一个 segment，并输出当前 segment 标题，包括 0 字节文件。
- 日志内容只做 HTML escape，不折叠、不省略、不规整空白行，页面展示应忠实反映文件内容。
- 日志正文流式输出期间必须周期性 yield/feed Watchdog，避免大日志页面长请求触发看门狗。

状态页/API：

- 内置系统首页的 heap、flash、FS、FileLog 容量只显示 KB/MB/B 人性化格式，不重复 raw bytes。
- OTA 进度和 API 大数字字节数可继续包含 raw bytes 和人性化格式。
- JSON 字段建议同时提供 `bytes` 和 `human`，避免前端重复实现单位转换。

系统首页：

- `/esp32base` 展示固件、profile、hostname、uptime、boot count、reset/wake reason 及中文说明、heap、flash、WiFi/IP/RSSI、FS、FileLog、NTP、OTA 等当前 profile 可用信息。
- 未启用的模块不显示对应行，避免非 FULL profile 引入额外依赖。

页面风格：

- 单栏设备控制台布局，正文、顶部导航和底部系统入口使用同一页面宽度。
- 默认正文 16px，标题 24px/21px/18px，兼顾桌面和移动端可读性。
- 默认页面最大宽度 1040px，浅灰背景配白色 panel，避免正文和操作块错位。
- 顶部导航使用中性色和浅绿灰 active 状态。
- 内置状态、WiFi、Auth、OTA、Logs、Tools 页面使用统一标题、panel、按钮和表单节奏；页面标题本身使用白色 header panel，与正文 panel 内边距对齐。
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
- handler 外调用 begin/end chunked 响应会安全失败或 no-op，并记录 WARN。
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
- `HOME_APP`：`/` 和 `/esp32base` 优先跳转业务首页，基础功能通过系统导航入口访问。
- `HOME_COMBINED`：`/` 跳转业务首页，`/esp32base` 使用同一套导航框架展示设备融合首页。
- 配置业务首页后，导航品牌链接指向业务首页，业务主导航不会重复显示同一个首页入口。
- `SYSTEM_NAV_SECTION` 会在页面底部以小字系统入口与 `Free heap` 同行展示；窄屏下系统入口可自然换行，避免遮挡和横向滚动。
- 基础库页面复用同一套导航框架，业务页和系统页保持一致入口结构。
- `/esp32base` 系统页展示固件、profile、hostname、uptime、boot count、reset/wake reason 及中文说明、heap、flash，以及当前 profile 可用的 WiFi、FS、FileLog、NTP、OTA 状态。
- `/esp32base/api/status` 保留 `resetReason` / `wakeReason` 原始字段，并提供 `resetReasonText` / `wakeReasonText` 中文说明字段。

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

## 8. JSON 输出

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

## 9. WiFi 配网状态机

进入 AP/config portal 的条件：

- 没有已保存 WiFi 凭证。
- 应用显式调用 `startConfigPortal()`。
- 用户显式清除凭证并在应用策略中选择进入 portal。

有已保存凭证但连接失败时：

- 不自动进入 AP/config portal。
- 保存的 SSID 或密码为空时视为无有效凭证。
- 持续 STA 重连。
- 单次 STA 连接尝试有非阻塞超时，默认 `ESP32BASE_WIFI_CONNECT_TIMEOUT_MS=15000`。
- 前几次短间隔重连，连续失败后进入长间隔 backoff。
- backoff 必须非阻塞，不影响 `Esp32Base::handle()` 和 sleep 决策。

提交新凭证成功后：

- 同步保存凭证；保存失败则返回错误，不切换连接。
- 停止 config portal。
- 切换到 STA 连接流程。

进入 deep sleep 后：

- STA、AP、DNS、Web 全部不可访问。
- 唤醒后重新执行 begin/handle 流程。

## 10. Handler 性能边界

Arduino `WebServer` 是同步模型。

自定义 handler 建议：

- 单次执行 < 200ms。
- 不在 handler 中做长时间阻塞 IO。
- 长任务采用“启动任务 -> 返回 task id -> 轮询状态”的方式。
- Web handle 对超过 250ms 的慢请求输出日志。

这属于已知限制，文档和示例必须体现。
