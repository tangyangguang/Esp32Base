# Esp32Base 能力变更记录

本文从 2026-05-06 起记录 Esp32Base 新增和优化的能力，面向正在接入本库的业务项目。业务项目应优先查看本文，了解最近可用的新 API、行为变化和推荐接入方式。

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
- 可覆盖 Home/WiFi/OTA/Logs/Reboot 标签，适配中文业务项目。

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
