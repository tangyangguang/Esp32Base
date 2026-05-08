# API 契约

本文锁定第一版最终实现的公开 API 形态。发布后维护阶段不得随意改变这些边界。

## 1. 头文件

统一公开入口：

```cpp
#include <Esp32Base.h>
```

不提供 `Esp32BaseAliases.h` 或 `Log`、`Config`、`Web` 等全局短别名。用户应使用完整类名，避免和 Arduino、第三方库或应用自己的全局符号冲突。

## 2. Esp32Base

职责：

- 固件信息。
- hostname。
- profile 信息。
- begin / handle。
- 启动诊断和资源日志。

建议 API：

```cpp
class Esp32Base {
public:
    static bool begin();
    static void handle();

    static void setFirmwareInfo(const char* name, const char* version, const char* build = nullptr);
    static const char* firmwareName();
    static const char* firmwareVersion();
    static const char* firmwareBuild();

    static void setHostname(const char* hostname);
    static const char* hostname();

    static const char* profileName();
    static bool isReady();
    static const char* lastError();

    static void logStartupConfig();
    static void logResources();
};
```

约定：

- `setHostname()` 仅设置运行时 hostname，不写 NVS。
- 应用应在 `Esp32Base::begin()` 前调用 `setHostname()`。
- 应用如需持久化 hostname，应自行通过 `Esp32BaseConfig` 保存，并在 begin 前恢复。
- mDNS 启动时读取当前 hostname；WiFi connected 后再修改 hostname 不要求自动重启 mDNS。
- `lastError()` 返回最近一次 begin 或关键模块启动失败原因；无错误时返回空字符串，不返回 `nullptr`。

## 3. Esp32BaseLog

### 3.1 编译期级别

日志级别：

```cpp
#define ESP32BASE_LOG_NONE    0
#define ESP32BASE_LOG_ERROR   1
#define ESP32BASE_LOG_WARN    2
#define ESP32BASE_LOG_INFO    3
#define ESP32BASE_LOG_DEBUG   4
#define ESP32BASE_LOG_VERBOSE 5
```

默认：

```cpp
ESP32BASE_LOG_LEVEL=ESP32BASE_LOG_INFO
```

关闭级别的日志不得保留 format 字符串到 `.rodata`。

### 3.2 日志宏

```cpp
ESP32BASE_LOG_E(tag, fmt, ...)
ESP32BASE_LOG_W(tag, fmt, ...)
ESP32BASE_LOG_I(tag, fmt, ...)
ESP32BASE_LOG_D(tag, fmt, ...)
ESP32BASE_LOG_V(tag, fmt, ...)
```

tag 可以为 `nullptr`，实现应退化为空字符串或默认 tag。

### 3.3 运行时 API

```cpp
class Esp32BaseLog {
public:
    enum Level : uint8_t {
        NONE = 0,
        ERROR = 1,
        WARN = 2,
        INFO = 3,
        DEBUG = 4,
        VERBOSE = 5
    };

    using Sink = void (*)(Level level, const char* tag, const char* message);
    using LineSink = void (*)(Level level, const char* tag, const char* message, const char* line);
    using TimeProvider = const char* (*)();

    static bool begin(uint32_t baud = ESP32BASE_LOG_BAUD);
    static void setRuntimeLevel(Level level);
    static Level runtimeLevel();
    static void setSink(Sink sink);
    static void setInternalLineSink(LineSink sink);
    static void setTimeProvider(TimeProvider provider);

    static void write(Level level, const char* tag, const char* fmt, ...);
    static void formatBytes(uint64_t bytes, char* out, size_t len);
    static void formatMillis(uint32_t ms, char* out, size_t len);
    static void formatUptime(uint32_t ms, char* out, size_t len);
};
```

运行时级别只能进一步过滤，不能突破编译期上限。

输出目标：

- Serial。
- 可选 sink callback。
- Runtime/FS profile 中由 `Esp32BaseFileLog` 提供文件日志；Core Log 本身不依赖 FS。

日志格式要求：

- 输出应按启动顺序、状态迁移顺序和错误链路组织，方便串口观察系统正在做什么。
- 默认格式包含 uptime、level、tag、message；对齐和间隔应保持稳定，便于人工扫描。
- `DEBUG` 编译级别下，启动流程应输出模块初始化顺序和 Web/NTP/mDNS/OTA 等延迟启动原因；默认 `INFO` 构建不输出这些开发诊断日志。
- message 按调用方传入内容输出，不规整 CR/LF；需要单行日志时由调用方传入单行 message。
- 大数字字节数必须使用人性化显示，例如 `1048576 bytes (1.00 MB)`；Web 页面与 JSON 中同样遵守该规则。
- `formatBytes()` 采用二进制单位，`1 KB = 1024 bytes`，`1 MB = 1024 KB`。
- WiFi 名称/密码、Web Auth 用户名/密码在 `INFO` 日志中明文输出，这是本库用于业务接入和现场调试的设计。
- sink callback 在日志调用现场同步执行，用户 sink 不得长时间阻塞。

### 3.4 Esp32BaseFileLog

仅在 `ESP32BASE_ENABLE_FILELOG=1` 时可用，依赖 `Esp32BaseFs`。默认文件为 `/logs/eb_app.log`，`4 × 32KB` 轮转，文件等级 WARN。INFO/DEBUG 文件日志使用 1KB / 2s 缓存，不做节流。

```cpp
class Esp32BaseFileLog {
public:
    static bool begin();
    static bool enable(const char* path, uint32_t maxBytes, Esp32BaseLog::Level fileLevel, uint8_t rotateFiles);
    static void disable();
    static bool flush();
    static bool clear();
    static void handle();
    static bool isEnabled();
    static const char* path();
    static uint32_t maxBytes();
    static uint8_t rotateFiles();
    static Esp32BaseLog::Level level();
    static const char* levelName();
    static uint32_t size();
    static uint32_t segmentSize(uint8_t index);
    static bool bufferEnabled();
    static uint16_t bufferSize();
    static uint16_t bufferUsed();
    static uint32_t flushIntervalMs();
};
```

## 4. Esp32BaseConfig

后端：ESP32 NVS。

限制：

- namespace 长度 `1..15`。
- key 长度 `1..15`。
- string value 可见内容长度不超过 3999 字节。
- 库内部 namespace 全部使用 `eb_` 前缀，例如 `eb_wifi`、`eb_sys`、`eb_log`。
- namespace 已表达库和模块归属，key 不重复模块前缀，例如 `eb_wifi.ssid`、`eb_wifi.pass`、`eb_sys.rst_cnt`、`eb_sys.wdt_cnt`、`eb_log.en`、`eb_log.path`、`eb_log.max`、`eb_log.files`、`eb_log.level`、`eb_web.auth_user`、`eb_web.auth_pass`。
- 应用不得使用 `eb_` 前缀，避免被库维护 API 清理。

建议 API：

```cpp
class Esp32BaseConfig {
public:
    static bool begin();
    static void handle();
    static bool isReady();

    static bool setStr(const char* ns, const char* key, const char* value);
    static bool getStr(const char* ns, const char* key, char* out, size_t len, const char* def = "");

    static bool setInt(const char* ns, const char* key, int32_t value);
    static int32_t getInt(const char* ns, const char* key, int32_t def = 0);
    static bool setIntDeferred(const char* ns, const char* key, int32_t value, uint32_t delayMs = 1000);

    static bool setBool(const char* ns, const char* key, bool value);
    static bool getBool(const char* ns, const char* key, bool def = false);
    static bool setBoolDeferred(const char* ns, const char* key, bool value, uint32_t delayMs = 1000);

    static bool setStrDeferred(const char* ns, const char* key, const char* value, uint32_t delayMs = 1000);

    static bool flushNextDue();
    static bool flushNextForced();
    static bool flushAll();

    static uint8_t pendingCount();
    static uint8_t pendingCapacity();

    static bool clearNamespace(const char* ns);
    static bool clearLibraryNamespaces();

    static void pauseDeferredFlush();
    static void resumeDeferredFlush();
    static bool isDeferredFlushPaused();
};
```

OTA 上传期间 deferred flush 暂停。

deferred 语义：

- `setXxxDeferred()` 写入 pending 队列后立即对读取可见。
- `getXxx()` 必须先查 pending 队列，未命中再读 NVS，因此 set 后立即 get 总是返回最新值。
- `getStr()` 找不到 key 时返回 false，并把默认值写入输出缓冲；`getInt()` / `getBool()` 找不到 key 时返回默认值。
- NVS 读取必须先判断 key 是否存在，避免 Arduino `Preferences` 在缺 key 时输出底层 `NOT_FOUND` 噪音。
- 字符串读取和写前比较使用固定 scratch buffer，不使用 Arduino `String` 拼接或读取，减少配置高频读取造成的 heap 碎片风险。
- 同一 namespace/key 的 pending 写入会合并为最新值，不重复占用多条 pending。
- OTA 上传期间只暂停 deferred flush；`getXxx()`、`pendingCount()`、`flushAll()` 和 `clearLibraryNamespaces()` 仍按各自语义工作。
- `clearLibraryNamespaces()` 只清理库内部 `eb_` 前缀 namespace，不清理业务 namespace。

## 5. Esp32BaseSystem

`Esp32BaseSystem` 不公开 OTA mark / rollback API。OTA 语义通过 `Esp32BaseOta` 暴露。

建议 API：

```cpp
class Esp32BaseSystem {
public:
    static bool begin();
    static bool isReady();

    static uint32_t freeHeap();
    static uint32_t minFreeHeap();
    static uint32_t totalHeap();
    static uint32_t flashSize();
    static uint32_t uptimeMs();
    static uint32_t bootCount();

    static const char* resetReason();
    static const char* resetReasonText();
    static const char* wakeReason();
    static const char* wakeReasonText();

    static void restart(const char* reason);

    static bool appendRestartLog(const char* reason);
    static uint8_t restartLogCount();
};
```

`bootCount()` 返回无符号启动会话计数，存储在 `eb_sys.boot_cnt`，使用 `uint32_t` 语义每次固件启动加 1。上电、手动复位、`ESP.restart()`、Watchdog、OTA 重启、deep sleep 唤醒都会计入；具体启动原因通过 `resetReason()` / `wakeReason()` 判断。

`resetReason()` 表示芯片本次为什么复位或重新启动，例如 `poweron`、`software`、`task_wdt`、`deep_sleep`。`resetReasonText()` 返回对应中文说明，例如 `上电启动`、`软件重启`、`看门狗复位`。

`wakeReason()` 只表示从 sleep 唤醒的来源，例如 `timer`、`ext0`、`gpio`；普通上电或普通复位没有 sleep 唤醒来源，因此返回 `undefined` 是正常状态。`wakeReasonText()` 返回对应中文说明，其中 `undefined` 对应 `非睡眠唤醒`。

`restartLogCount()` 返回库维护的重启/休眠生命周期日志计数，存储在 `eb_sys.rst_cnt`，最大饱和为 255。库内部保留最近 4 条 reason 作为诊断环。

## 6. Esp32BaseBus

仅在启用 Bus 时可用。

要求：

- `publish()` 只能在 Arduino loop 任务调用。
- `begin()` 记录 loop task handle。
- `publish()` 检查当前 task，不匹配则返回 false；开启 `ESP32BASE_BUS_STRICT_TASK_CHECK=1` 时 assert。
- `publish()` 先快照本次匹配的订阅者，再调用 callback；callback 中允许 `unsubscribe()`，已取消的后续订阅不会在本次 publish 中继续收到事件，新订阅不会收到当前正在发布的事件。

事件常量规则：

- 所有会 publish 事件的公开模块，必须在对应 class 内导出 `static constexpr const char* EVENT_*`。
- 事件名使用小写点分格式，例如 `wifi.connected`、`ota.progress`。
- 事件名最大可见长度为 31 字节；`subscribe()` 对更长事件名返回 0，避免静默截断后出现不可诊断的匹配问题。
- WiFi callback、timer callback、ISR 或 LWIP 回调不得直接 publish，必须入队后在 `Esp32Base::handle()` 中 publish。

建议 API：

```cpp
class Esp32BaseBus {
public:
    using Callback = void (*)(const char* event, const char* data, void* user);
    using SubscriptionId = uint16_t;

    static bool begin();
    static SubscriptionId subscribe(const char* event, Callback cb, void* user = nullptr);
    static bool publish(const char* event, const char* data = "");
    static bool unsubscribe(SubscriptionId id);
    static uint16_t subscriberCount();
    static uint16_t subscriberCapacity();
};
```

## 7. Esp32BaseWiFi

建议 API：

```cpp
class Esp32BaseWiFi {
public:
    enum State : uint8_t {
        IDLE,
        CONNECTING,
        CONNECTED,
        CONFIG_PORTAL,
        RETRY_BACKOFF,
        FAILED
    };

    static constexpr const char* EVENT_CONNECTED = "wifi.connected";
    static constexpr const char* EVENT_DISCONNECTED = "wifi.disconnected";
    static constexpr const char* EVENT_CONFIG_PORTAL = "wifi.config_portal";
    static constexpr const char* EVENT_RETRY = "wifi.retry";
    static constexpr const char* EVENT_FAILED = "wifi.failed";

    static bool begin();
    static void handle();
    static bool isReady();

    static bool connect(const char* ssid, const char* password, bool persist = true);
    static bool clearCredentials();
    static bool startConfigPortal();
    static bool stopConfigPortal();

    static State state();
    static const char* stateName();
    static bool isConnected();
    static const char* ssid();
    static bool ip(char* out, size_t len);
    static int32_t rssi();

    static void setPowerSave(bool enabled);
    static bool powerSave();
};
```

WiFi 凭证和重连策略：

- 无已保存凭证时，`begin()` 可进入 `CONFIG_PORTAL`。
- 有效凭证要求 SSID 非空且不超过 32 字节，密码非空且不超过 64 字节；超限输入返回 false，不静默截断。
- 有已保存凭证但连接失败时，不自动进入 AP/config portal，而是持续重连。
- 单次 STA 连接尝试有非阻塞超时，默认 `ESP32BASE_WIFI_CONNECT_TIMEOUT_MS=15000`。
- 前几次重连使用短间隔，连续失败后进入长间隔 backoff；backoff 必须非阻塞，不影响 `handle()`、Watchdog feed 和必要休眠。
- 只有显式 `clearCredentials()`、`startConfigPortal()` 或应用自定义策略，才能进入 AP/config portal。
- `clearCredentials()` 只清空库管理的 WiFi 凭证，不立即触发重连、断线或 portal。
- `connect(..., persist=true)` 必须先同步写入 NVS 保存凭证，写入失败时返回 false 且不切换连接；这是显式配置提交的可靠性取舍，避免页面提交后立即重启导致新凭证丢失。
- `INFO` 日志明文输出 SSID 和密码，便于业务接入和现场调试。
- 进入 deep sleep 后 STA、AP、DNS、Web 均不可用；唤醒相当于新一轮启动，按凭证状态恢复网络。
- 默认无限重试，`FAILED` 状态在默认配置下不出现。
- 仅当应用显式设置有限 maxRetries 且全部用尽，才进入 `FAILED`。
- 从 `FAILED` 恢复必须通过 `connect()` 重新提交凭证，或 `clearCredentials()` 后再显式 `startConfigPortal()`。

## 8. Esp32BaseDns / Ntp / Mdns

```cpp
class Esp32BaseDns {
public:
    static bool begin();
    static void handle();
    static void stop();
    static bool isRunning();
};

class Esp32BaseNtp {
public:
    static bool begin();
    static bool isStarted();
    static bool isTimeSynced();
    static uint32_t timestamp();
    static void setServers(const char* s1, const char* s2 = nullptr, const char* s3 = nullptr);
    static bool formatTime(char* out, size_t len, const char* fmt);
};

class Esp32BaseMdns {
public:
    static bool begin();
    static void stop();
    static bool isRunning();
    static bool addHttpService(uint16_t port = 80);
};
```

`Esp32BaseDns` 在 config portal 模式下对所有 DNS 查询返回 AP IP，不维护固定域名白名单，也不提供 `addCaptiveTarget()`。

NTP 默认使用 UTC+8，即 `ESP32BASE_NTP_GMT_OFFSET_SEC=(8L * 3600L)`、`ESP32BASE_NTP_DAYLIGHT_OFFSET_SEC=0L`；应用可在 include 前用宏覆盖。

`isTimeSynced()` 默认要求当前 epoch >= `ESP32BASE_NTP_SYNC_MIN_EPOCH`，该宏默认 `1700000000UL`，避免把明显未同步或异常回退的时间误判为已同步。

NTP 日志策略：

- `isTimeSynced()` 是状态查询，不代表发生了一次 NTP 同步尝试；未同步状态不输出周期性 WARN/DEBUG。
- `ntp_client_started` 和对时成功日志使用 INFO。
- 只有接入明确的单次同步失败事件时，才应为该失败事件输出 WARN。

NTP 对时成功日志必须包含：

- 当前实际日期时间。
- 当前 uptime。
- 推算出的 boot wall time。

日志时间戳在 NTP 对时前使用启动后的 `millis()`，例如 `[42442]`；NTP 对时成功后切换为绝对日期时间，例如 `[2026-05-05 12:45:55]`。
对时成功时的 `time_mapping` 日志用于把早期毫秒时间轴换算为实际时间。

## 9. Esp32BaseWeb

```cpp
class Esp32BaseWeb {
public:
    static constexpr const char* EVENT_READY = "web.ready";
    static constexpr const char* EVENT_STOPPED = "web.stopped";

    enum Method : uint8_t {
        METHOD_UNKNOWN,
        METHOD_GET,
        METHOD_POST,
        METHOD_ANY
    };

    enum HomeMode : uint8_t {
        HOME_ESP32BASE,
        HOME_APP,
        HOME_COMBINED
    };

    enum SystemNavMode : uint8_t {
        SYSTEM_NAV_TOP,
        SYSTEM_NAV_BOTTOM,
        SYSTEM_NAV_SECTION
    };

    enum BuiltinPage : uint8_t {
        BUILTIN_HOME,
        BUILTIN_WIFI,
        BUILTIN_OTA,
        BUILTIN_LOGS,
        BUILTIN_REBOOT,
        BUILTIN_TOOLS = BUILTIN_REBOOT,
        BUILTIN_SYSTEM,
        BUILTIN_AUTH
    };

    using Handler = void (*)();

    static bool begin();
    static void handle();
    static bool isReady();

    static void setDefaultAuth(const char* user, const char* pass);
    static const char* authUser();
    static bool isAuthEnabled();
    static void setAuthEnabled(bool enabled);
    static bool checkAuth();
    static bool verifyAuth();
    static bool verifyAuth(const char* user, const char* pass);
    static bool saveAuth(const char* user, const char* pass);
    static bool resetAuth();

    static bool addRoute(const char* path, Method method, Handler handler);
    static bool addPage(const char* path, const char* title, Handler handler);
    static bool addApi(const char* path, Handler handler);
    static bool addNavItem(const char* path, const char* title);

    static bool setDeviceName(const char* name);
    static bool setHomePath(const char* path);
    static void setHomeMode(HomeMode mode);
    static void setSystemNavMode(SystemNavMode mode);
    static bool setBuiltinLabel(BuiltinPage page, const char* label);
    static void setHeadExtraCallback(Handler handler);

    static Method currentMethod();
    static bool isMethod(Method method);
    static const char* currentMethodName();

    static bool hasParam(const char* name);
    static bool getParam(const char* name, char* out, size_t len);
    static bool getRequestBody(char* out, size_t len);

    static void sendHeader(const char* title = nullptr);
    static void sendFooter();
    static bool beginResponse(int code, const char* contentType, const char* filename = nullptr);
    static bool beginText(int code);
    static bool beginCsv(int code, const char* filename = nullptr);
    static void endResponse();
    static void sendChunk(const char* text);
    static void sendBytes(const uint8_t* data, size_t len);
    static void writeHtmlEscaped(const char* text);
    static void writeCsvEscaped(const char* text);

    static void sendText(int code, const char* text);
    static void sendHtml(int code, const char* html);
    static void sendJson(int code, const char* json);
    static void beginJson(int code);
    static void writeJsonEscaped(const char* text);
    static void endJson();
};
```

Route 缓冲机制：

- `addRoute()` 始终写入静态路由表。
- path 必须以 `/` 开头，最大可见长度为 47 字节；更长路径返回 false，避免静默截断。
- Web 未启动时缓存。
- Web 启动时一次性注册。
- Web 已启动时立即注册。
- `addPage()` 注册业务页面并默认进入业务导航；`addNavItem()` 可把应用已有路由加入业务导航但不注册 handler。
- `addPage(path, title, handler)` 必须由应用显式提供短标题，例如 `Fan`、`Config`。
- title 最大可见长度为 23 字节；空 title 返回 false。
- `addRoute()` 和 `addApi()` 不进入业务入口列表，避免 API 或隐藏路由污染导航。
- `setDeviceName()` 设置导航品牌和默认标题；`setHomePath()` 设置业务首页路径。
- `setHomeMode(HOME_ESP32BASE)` 保持基础库首页默认行为；`HOME_APP` 让 `/` 和 `/esp32base` 优先进入业务首页；`HOME_COMBINED` 让 `/` 进入业务首页，并保留 `/esp32base` 为融合首页。
- `setSystemNavMode()` 控制基础功能入口位置：顶部、底部或底部紧凑系统工具区；`SYSTEM_NAV_SECTION` 会把系统入口作为小字链接与 `Free heap` 放在同一 footer 区域，窄屏可自然换行。
- `setBuiltinLabel()` 覆盖内置导航标签，可用于中文本地化；`BUILTIN_TOOLS` 是维护页标签，`BUILTIN_REBOOT` 作为旧名称别名保留。
- `setHeadExtraCallback()` 设置额外 head 输出回调；`sendHeader()` 在默认 `WEB_HEAD` 后、`</head><body>` 和顶部导航前调用它，业务项目可在这里输出 `<style>`，避免页面刷新时先显示基础库默认导航样式。
- 顶部导航会给当前匹配项输出 `active` class；匹配规则为 path 完全相等，或当前路径以 `path + "/"` 开头，多个匹配时选择最长 path。`SYSTEM_NAV_SECTION` 的底部系统维护菜单不参与 active 业务导航状态。
- `/esp32base` Status 页展示固件、profile、hostname、uptime、boot count、reset/wake reason 及中文说明、heap、flash，以及当前 profile 可用的 WiFi、FS、FileLog、NTP、OTA 状态；页面容量值只显示 KB/MB/B 人性化格式。
- `/esp32base/tools` Tools 页承载维护操作，包括重启设备；启用 FS 的 profile 还提供手动格式化 LittleFS 操作，会清除日志和所有 LittleFS 文件，但不清除 WiFi、Web Auth 或 NVS 配置。
- `/esp32base/auth` 是内置认证管理页面，受当前 Basic Auth 保护，提交成功后新账号密码立即生效。
- Web Auth 认证优先级为：已保存认证 > 应用默认认证 > 库默认 `admin/admin`。
- `setDefaultAuth(user, pass)` 设置应用默认认证；如果用户已保存认证，不会覆盖已保存认证。
- `authUser()` 只返回当前用户名，不提供读取密码 API；明文密码通过 INFO 日志输出。
- `verifyAuth(user, pass)` 校验显式传入的账号密码；无参 `verifyAuth()` 仍表示当前请求是否已认证。
- `saveAuth(user, pass)` 保存 Web Auth 到 `eb_web.auth_user`、`eb_web.auth_pass`，并立即切换为新认证。
- `resetAuth()` 清除 `eb_web` 持久化 Auth，并恢复应用默认认证；没有应用默认认证时恢复库默认 `admin/admin`。
- Web Auth 明文密码会持久化到 `eb_web.auth_pass`，不输出到 HTML、JSON 或 API 响应；`INFO` 日志明文输出 Web 用户名和密码，便于业务接入和现场调试。
- `METHOD_ANY` 用于同一路径 GET/POST 复用；应用 handler 内可用 `currentMethod()`、`isMethod()` 或 `currentMethodName()` 判断当前请求方法。
- `currentMethod()` 仅在 handler 上下文中返回实际方法：GET 为 `METHOD_GET`，POST 为 `METHOD_POST`；handler 外或未知方法返回 `METHOD_UNKNOWN`。
- `isMethod(METHOD_ANY)` 在有效 handler 请求中返回 true；`METHOD_ANY` 不作为实际请求方法返回。
- `beginResponse(code, contentType, filename)` 开始通用 chunked 响应，后续使用 `sendChunk()` 输出文本或 `sendBytes()` 输出二进制块，最后必须调用 `endResponse()`。
- `beginText(code)` 等价于 `text/plain; charset=utf-8` chunked 响应；`beginCsv(code, filename)` 等价于 `text/csv; charset=utf-8`，filename 非空时发送 `Content-Disposition: attachment`。
- `beginResponse()` / `beginText()` / `beginCsv()` 只能在 handler 请求上下文中成功；handler 外、contentType 为空或超过 63 字节、filename 含不安全字符时返回 false 并记录 WARN。
- CSV 字段必须用 `writeCsvEscaped()` 输出，避免逗号、换行或双引号破坏导出格式。
- `beginJson(code)` 的状态码必须在 `endJson()` 发送时保留。

内置页面交互要求：

- WiFi 配置页面必须回显当前 SSID 和密码。
- WiFi 配置提交必须校验 SSID 非空、密码非空；空值不得提交。
- 重启按钮必须有二次确认，可用浏览器端 JavaScript 实现。
- API 中的字节数可保留 raw bytes；内置页面面向人工查看时优先只显示 KB/MB/B 人性化格式，避免重复。
- `sendHeader()` 输出的默认 input 样式只覆盖文本类控件；checkbox、radio、file、range、color、hidden 等非文本控件不被拉伸成文本输入框。
- `sendHeader()` 的基础样式保持简洁中性：普通链接不默认渲染为蓝色按钮，导航 active 使用浅绿灰背景和深绿灰文字；业务视觉仍推荐通过 `setHeadExtraCallback()` 注入 CSS。

## 10. Esp32BaseOta

```cpp
class Esp32BaseOta {
public:
    static constexpr const char* EVENT_START = "ota.start";
    static constexpr const char* EVENT_PROGRESS = "ota.progress";
    static constexpr const char* EVENT_SUCCESS = "ota.success";
    static constexpr const char* EVENT_FAILED = "ota.failed";

    enum Status : uint8_t {
        IDLE,
        READY,
        UPLOADING,
        VERIFYING,
        SUCCESS,
        FAILED
    };

    static bool begin();
    static void handle();
    static bool isReady();
    static bool isUploading();
    static Status status();

    static bool startUpload(size_t totalSize, const char* expectedSha256Hex = nullptr);
    static bool writeChunk(const uint8_t* data, size_t len);
    static bool finishUpload();
    static void abortUpload(const char* reason);

    static uint8_t progress();
    static size_t bytesProcessed();
    static size_t totalSize();
    static const char* lastError();

    static bool markCurrentValid();
    static bool isRollbackPossible();
    static void rollbackAndRestart(const char* reason);
};
```

`Update.end(true)` 必须在 SHA256 校验通过、且实际接收字节数等于声明总大小之后调用。

`expectedSha256Hex` 可为空；不为空时必须是 64 字节十六进制字符串，大小写均接受，内部统一按小写比较。

Web OTA 上传页面不要求额外认证；它只复用 Web 层 Basic Auth。上传过程必须提供进度展示。

## 11. Esp32BaseWatchdog / Sleep / Fs / Health

```cpp
class Esp32BaseWatchdog {
public:
    static bool begin(uint32_t timeoutMs);
    static void feed();
    static bool removeCurrentTaskForLongOperation();
    static bool restoreCurrentTaskAfterLongOperation();
    static bool isEnabled();
    static bool wasWatchdogReset();
    static uint32_t lifetimeResetCount();
};

class Esp32BaseSleep {
public:
    static bool begin();
    static bool deepSleepSeconds(uint32_t seconds);
    static bool deepSleepUs(uint64_t us);
    static bool enableTimerWakeup(uint64_t us);
    static bool enableGpioWakeup(int gpio, int level);
};
```

`Esp32BaseWatchdog::begin(timeoutMs)` 要求 `timeoutMs >= 1000`；更小值返回 false 并输出 WARN，避免 Arduino ESP32 2.x 下秒级 WDT 参数被截断为 0。

`begin()` 是轻量初始化，只记录 Sleep 模块进入统一生命周期管理，不配置任何默认 wake source。

`deepSleepSeconds()` / `deepSleepUs()` 不是 IDF deep sleep API 的 thin wrapper。它们必须走统一生命周期流程：

1. 发布生命周期事件，如 Bus 启用。
2. 写重启日志环。
3. `Esp32BaseConfig::flushAll()`。
4. 输出最后诊断。
5. 处理 Watchdog。
6. 调用 `esp_deep_sleep_start()`。

用户代码禁止直接调用 `esp_deep_sleep_start()` 绕开库流程。

```cpp
class Esp32BaseFs {
public:
    static bool begin();
    static bool isReady();
    static bool format();

    // text
    static bool writeFile(const char* path, const char* content);
    static bool readFile(const char* path, char* out, size_t len);
    static bool appendFile(const char* path, const char* content);

    // binary
    static bool writeBytes(const char* path, const uint8_t* data, size_t len);
    static bool readBytes(const char* path, uint8_t* out, size_t maxLen, size_t* readLen);
    static bool appendBytes(const char* path, const uint8_t* data, size_t len);
    static bool readBytesAt(const char* path, uint32_t offset, uint8_t* out, size_t maxLen, size_t* readLen);
    static bool writeBytesAt(const char* path, uint32_t offset, const uint8_t* data, size_t len);

    // metadata
    static bool removeFile(const char* path);
    static bool rename(const char* from, const char* to);
    static bool exists(const char* path);
    static int64_t fileSize(const char* path);

    // directory
    using ListCallback = void (*)(const char* name, size_t size, bool isDir, void* user);
    static bool listDir(const char* path, ListCallback cb, void* user = nullptr);
    static bool mkdir(const char* path);
    static bool rmdir(const char* path);

    // capacity
    static size_t totalBytes();
    static size_t usedBytes();
    static size_t freeBytes();
};

class Esp32BaseHealth {
public:
    static constexpr const char* EVENT_TICK = "health.tick";

    static bool begin();
    static void handle();
    static uint32_t lastTickMs();
    static uint32_t loopPeriodMaxMs();
};
```

`format()` 会格式化 LittleFS 分区并清除已有文件；基础库默认 `begin()` 不自动格式化，避免误删业务数据。示例工程可在确认是首次调试或可丢弃文件内容时显式调用。

FS 未成功 `begin()` 时，文件和目录操作返回失败，容量查询返回 0，不隐式格式化文件系统。

`listDir()` 遍历目录时会在每次 callback 后显式关闭当前 `File`，避免长目录遍历时积累底层句柄。

Offset binary API 用于业务二进制定长记录、分页读取和环形覆盖写入，不要求业务 include `LittleFS.h` 或 Arduino `File`：

- `readBytesAt()` 打开已存在文件并从 `offset` 读取最多 `maxLen` 字节，实际读取长度写入 `readLen`；读到 EOF 前允许短读并返回 true。
- `readBytesAt()` 在 FS 未 ready、path 非绝对路径、文件不存在、out 为空或 `offset > fileSize` 时返回 false；失败时 `readLen` 为 0。`offset == fileSize` 返回 true 且读取 0 字节。
- `writeBytesAt()` 只覆盖已存在文件中的现有字节，不隐式创建文件、不扩展文件、不填洞；FS 未 ready、path 非绝对路径、文件不存在、data 为空但 len 非 0、`offset + len > fileSize` 或底层写失败时返回 false。
- 需要固定容量环形文件时，应用可先用 `writeBytes()` 或分块 `appendBytes()` 初始化文件，再用 `writeBytesAt()` 覆盖记录槽位。

Health 仅负责周期性采样、loop 周期统计和发布 `health.tick` 事件。heap、reset reason、WiFi state、FS state 等详情通过对应模块查询，Health 不重复包装所有字段。

Health tick 日志策略：

- 每个 tick 窗口内统计一次最大 loop 间隔。
- 未超过 `ESP32BASE_HEALTH_LOOP_WARN_MS` 时，默认每 30 分钟以 DEBUG 输出一次 `tick loopMax=...`，其中 `loopMax` 是这段 DEBUG 日志周期内普通 tick 窗口的最大值。
- 超过阈值时，以 WARN 输出 `loop_slow loopMax=... threshold=...`。
- 默认 WARN 阈值为 3000ms，避免普通 Web 请求造成健康日志刷屏，同时能及时暴露明显卡顿；业务项目可按控制实时性要求覆盖为 1000/2000/5000ms。
- `ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS` 默认 1800000ms；设为 0 可关闭普通 DEBUG tick 日志，不影响 WARN 慢循环日志。
- `loopPeriodMaxMs()` 仍返回启动以来最大 loop 间隔，不随 tick 窗口清零。
