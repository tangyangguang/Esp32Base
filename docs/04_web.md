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
- 生产固件应在 `Esp32Base::begin()` 前调用 `Esp32BaseWeb::setAuth()`。

OTA 额外要求：

- 应用必须显式调用 `Esp32BaseWeb::setAuth()`。
- `Esp32BaseWeb::isAuthSetByApplication()` 必须为 true。
- 否则 OTA route 不注册。
- OTA 上传页不再叠加第二套认证，只复用 Web Basic Auth。

不使用“密码是否等于默认字符串”作为唯一判断。

安全说明：

- HTTP Basic Auth 明文传输。
- 它只用于防误操作。
- 不抵御 LAN 内主动攻击。
- 生产固件必须显式设置认证信息。

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

OTA:

- `GET /esp32base/ota`
- `POST /esp32base/ota`
- `GET /esp32base/api/ota`

Logs:

- `GET /esp32base/logs`
- `POST /esp32base/logs/clear`

Lifecycle:

- `GET /esp32base/reboot`
- `POST /esp32base/reboot`

OTA 路由只在启用 OTA 且认证条件满足时注册。

## 7. 内置页面交互

WiFi 配置页：

- 回显当前 SSID。
- 回显当前密码。
- 表单提交前用前端 JS 校验 SSID 非空、密码非空。
- 服务端 API 必须重复校验 SSID 非空、密码非空。
- 空 SSID 或空密码返回错误，不保存凭证。
- SSID 超过 32 字节或密码超过 64 字节时返回错误，不静默截断。

重启操作：

- 页面按钮必须有二次确认。
- 二次确认可使用浏览器端 JavaScript。
- API 层仍建议要求 POST，不使用 GET 触发重启。

OTA 上传页：

- 使用 Web Basic Auth，不额外要求单独认证。
- 上传中显示进度。
- 进度同时显示百分比、已处理字节数和总字节数。
- 字节数必须同时给出 raw bytes 和 KB/MB 人性化格式。

Logs 页面：

- 需要 Basic Auth。
- FS/FileLog 不可用时显示 `File log: unavailable`。
- 读取日志内容前必须调用 `Esp32BaseFileLog::flush()`。
- 显示 enabled、path、file level、rotate files、buffer used/total、flush interval、max per file、max total、每段大小。
- 以 history/current 顺序展示日志内容。
- 日志内容必须 HTML escape。
- 清空日志只接受 POST，表单使用 `confirm()` 和 `once(form)`，成功后 303 redirect。

状态页/API：

- heap、flash、FS、OTA 大数字字节数必须包含 raw bytes 和人性化格式。
- JSON 字段建议同时提供 `bytes` 和 `human`，避免前端重复实现单位转换。

页面风格：

- 单栏布局。
- 移动端友好。
- 顶部导航。
- 简洁按钮和表单。
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
Esp32BaseWeb::writeJsonEscaped(text);
Esp32BaseWeb::endJson();
```

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
