# API 说明

## 1. Esp32Base

```cpp
Esp32Base::setFirmwareInfo("app", "1.0.0", "build");
Esp32Base::setHostname("esp32base-demo");
Esp32Base::begin();
Esp32Base::handle();
```

常用接口：

- `begin()`：初始化所有基础模块。
- `handle()`：主循环处理入口。
- `setFirmwareInfo()`：设置固件名称、版本和构建信息。
- `setHostname()` / `hostname()`：设置和读取 hostname。
- `logStartupConfig()` / `logResources()`：输出诊断日志。

## 2. Esp32BaseConfig

用于 NVS 小型配置。

```cpp
Esp32BaseConfig::setStr("app", "name", "demo");
Esp32BaseConfig::getStr("app", "name", buf, sizeof(buf), "");
Esp32BaseConfig::setIntDeferred("app", "boots", 1);
Esp32BaseConfig::flush();
```

规则：

- namespace 和 key 最长 15 字符。
- `esp32base` 是库内部保留 namespace，应用不要使用。
- 写入前会比较旧值，未变化则跳过写入。
- deferred 写入每次 `handle()` 最多提交一条。

## 3. Esp32BaseNetwork

用于 WiFi、NTP 和 mDNS。

```cpp
Esp32BaseNetwork::connect("ssid", "password");
Esp32BaseNetwork::isWiFiConnected();
Esp32BaseNetwork::ip();
Esp32BaseNetwork::setNtpServers("pool.ntp.org");
Esp32BaseNetwork::formatTime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S");
```

无保存 WiFi 凭证时会自动开启 AP 配置模式，默认 SSID 是 `Esp32Base-Config`。

## 4. Esp32BaseWeb

用于内置 Web 和应用扩展。

```cpp
Esp32BaseWeb::setAuth("admin", "admin123");
Esp32BaseWeb::addPage("/", handleHome);
Esp32BaseWeb::addApi("/api/ping", handlePing);
Esp32BaseWeb::sendJson(200, "{\"ok\":true}");
```

内置路径：

- `GET /esp32base`
- `GET /esp32base/api/status`
- `GET /esp32base/wifi`
- `POST /esp32base/wifi`
- `POST /esp32base/api/wifi`
- `GET /esp32base/ota`
- `POST /esp32base/ota`
- `POST /esp32base/reboot`

应用路由可以在 `Esp32Base::begin()` 前或后注册。

## 5. Esp32BaseRuntime

用于系统资源、Watchdog、deep sleep 和 LittleFS。

```cpp
Esp32BaseRuntime::freeHeap();
Esp32BaseRuntime::enableWatchdog(10000);
Esp32BaseRuntime::writeFile("/runtime.json", "{}");
Esp32BaseRuntime::readFile("/runtime.json", buf, sizeof(buf));
Esp32BaseRuntime::deepSleepSeconds(10);
```

Watchdog 默认不启用。启用后 `Esp32Base::handle()` 会自动喂狗。

LittleFS 会在 Runtime 初始化时尝试挂载。若分区表没有 LittleFS 分区，挂载失败只会输出 warning。
