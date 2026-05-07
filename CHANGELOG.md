# Esp32Base 能力变更记录

本文从 2026-05-06 起记录 Esp32Base 新增和优化的能力，面向正在接入本库的业务项目。业务项目应优先查看本文，了解最近可用的新 API、行为变化和推荐接入方式。

## 2026-05-07

### Logs 页面 segment 标签大小显示

优化：

- Logs 页面顶部 `current-0`、`history-N` 标签中的文件大小改为只显示 KB/MB/B 人性化值，不再同时显示 raw bytes。

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

- 可声明业务页面是设备主界面，基础库 WiFi、OTA、Logs、Reboot 是系统工具。
- 可设置 `/` 和 `/esp32base` 使用基础库首页、业务首页优先或融合首页。
- 可把系统工具放在顶部、底部或底部紧凑系统工具区。
- 可覆盖 Home/WiFi/OTA/Logs/Reboot/Auth 标签，适配中文业务项目。

关键行为：

- 未配置时保持 Esp32Base 默认首页和默认导航。
- `addPage()` 注册业务页面并进入业务导航；`addNavItem()` 只注册导航项，不注册路由。
- 配置业务首页后，导航品牌链接指向业务首页，业务主导航不会重复显示同一个首页入口。
- `HOME_APP` 和 `HOME_COMBINED` 下，设备根路径 `/` 都进入业务首页；`HOME_COMBINED` 仅保留 `/esp32base` 为融合首页。
- 内置页面和业务页面复用同一套 `sendHeader()` / `sendFooter()` 导航框架。
- `SYSTEM_NAV_SECTION` 使用底部小字系统入口，与 `Free heap` 同行展示；移动端允许系统入口自然换行，避免挤压和横向滚动。

推荐接入：

```cpp
Esp32BaseWeb::setDeviceName("Faucet");
Esp32BaseWeb::setHomePath("/status");
Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_COMBINED);
Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION);
Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_HOME, "系统");
Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_WIFI, "网络");
Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_LOGS, "日志");
Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_REBOOT, "重启");
Esp32BaseWeb::setBuiltinLabel(Esp32BaseWeb::BUILTIN_SYSTEM, "系统工具");
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
- Web Auth 可持久化到 `eb_web.auth_user`、`eb_web.auth_salt`、`eb_web.auth_hash`，密码不保存明文，只保存 salted SHA-256 摘要。
- Web Auth 更新后立即生效；浏览器缓存旧 Basic Auth 时，后续请求会重新触发认证。
- OTA 与 Auth 配置解耦：OTA route 按 profile/OTA 编译条件注册；Web Auth 开启时受 Basic Auth 保护，Web Auth 关闭时无密码保护。
- `clearLibraryNamespaces()` 会清理 `eb_web`，恢复出厂时同步清除持久化 Web Auth。

业务侧用途：

- 业务项目访问 `/esp32base` 可以直接看到设备基础状态，不需要另写基础诊断页。
- 现场查看日志时，日志页更容易判断轮转文件顺序、空 segment 和异常空白来源。
- 业务项目可用 `bootCount()` 判断固件累计启动会话次数，并结合 `resetReason()` / `wakeReason()` 判断本次启动来源。
- 业务项目可直接链接 `/esp32base/auth`，让用户修改设备认证账号/密码，不需要自行保存基础库鉴权数据。
- 业务项目可每次启动调用 `setDefaultAuth()` 提供设备默认认证，用户保存过认证后基础库会优先使用已保存认证。
