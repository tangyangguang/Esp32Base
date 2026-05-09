# OTA 与回滚

## 1. 定位

OTA 是 FULL profile 的核心能力，必须按量产可靠性设计。

OTA 不只是 Web 上传文件，还包括：

- 复用 Web Auth。
- ArduinoOTA/espota 命令行上传。
- SHA256。
- Watchdog 联动。
- Config deferred flush pause。
- rollback。
- 中途断电恢复。

## 2. 启动条件

OTA 依赖：

- WiFi connected。
- Web ready。

OTA route 按 profile/OTA 编译条件注册，不依赖应用是否设置默认认证，也不依赖 Web Auth 是否开启。

- Web Auth 开启时，OTA 页面和上传接口复用 Web Basic Auth。
- Web Auth 关闭时，OTA 页面和上传接口不要求认证，风险由应用和用户自行承担。

`ESP32BASE_ENABLE_ARDUINO_OTA` 默认跟随 `ESP32BASE_ENABLE_OTA`。启用 OTA 的 profile 默认同时支持 PlatformIO/espota：

```ini
upload_protocol = espota
upload_port = <hostname>.local
upload_flags =
  --auth=<password>
```

命令行 OTA 使用 `Esp32Base::hostname()`、标准端口 3232 和现有 mDNS host 记录；ArduinoOTA 自身不重复启动 mDNS。业务不需要命令行 OTA 时，可显式 `-D ESP32BASE_ENABLE_ARDUINO_OTA=0`。

espota 认证密码为当前生效的 Web Auth 密码；espota 没有用户名，因此 Web 用户名不参与。即使 Web Auth 被关闭，ArduinoOTA 仍要求密码，避免静默开放无密码 OTA。

Esp32Base 同时提供更快的命令行 Web OTA target。它复用现有 HTTP Web OTA 接口和 Web Basic Auth，不依赖 mDNS，推荐直接使用设备 IP：

```ini
extra_scripts =
  post:path/to/Esp32Base/scripts/esp32base_webota.py

custom_esp32base_webota_host = 192.168.2.112
custom_esp32base_webota_user = admin
custom_esp32base_webota_password = admin
```

运行：

```sh
pio run -t webota
```

`webota` target 由 Esp32Base 提供的可复用 PlatformIO extra_script 注册；业务项目不需要复制脚本或编写自定义 target。可用配置项：

- `esp32base_webota_url`：完整 URL，例如 `http://192.168.2.112/esp32base/ota`。
- `esp32base_webota_host`：设备 IP 或 hostname；未设置 URL 时使用。
- `esp32base_webota_port`：默认 `80`。
- `esp32base_webota_path`：默认 `/esp32base/ota`。
- `esp32base_webota_user` / `esp32base_webota_password`：默认 `admin` / `admin`。
- `esp32base_webota_auth`：可用 `user:password` 一次性设置 Basic Auth。
- `esp32base_webota_sha256`：默认 `auto`，可设为 `off` 或 64 字符 SHA256。
- `esp32base_webota_timeout`：默认 `120` 秒，用于预检和普通请求。
- `esp32base_webota_upload_timeout`：默认 `600` 秒，用于实际固件上传，弱网或大固件可单独调大。
- `esp32base_webota_chunk_size`：默认 `65536` 字节；HTTP 上传默认使用较大分块以提高反复烧录效率。
- `esp32base_webota_preflight`：默认 `true`，上传前通过状态接口快速校验连接和认证，避免错误密码时传完整包。
- `esp32base_webota_status_path`：默认 `/esp32base/api/ota`，用于预检。

配置优先级为当前 `platformio.ini` 环境的 `custom_esp32base_webota_*` 配置、`[esp32base_webota]` 公共段、环境变量。环境变量名使用配置项大写形式，例如 `ESP32BASE_WEBOTA_HOST`。

使用边界：

- `espota` 是 ArduinoOTA 标准协议，兼容 PlatformIO/ArduinoOTA 常规工具链。
- `webota` 是 Esp32Base 提供的 HTTP 上传方式，默认 64 KB 分块，通常比 espota 快；要求设备 Web 服务和 `/esp32base/ota` 可访问。
- 两者都不影响浏览器 Web OTA 页面。
- `webota` 命令行日志会显示友好容量单位、开始时间、结束时间、用时、平均速度，并按约 5% 粒度输出上传进度。
- 设备端会在 Web OTA 开始前检查下一 OTA 分区容量；固件大于可写 app slot 时直接失败。上传过程中实际写入字节数也不得超过声明总大小。
- `Update.begin()` 失败时会把底层 Update 错误字符串写入 `lastError()`，便于区分空间、flash 或 Update 状态问题。

## 3. 状态机

状态：

- `IDLE`
- `READY`
- `UPLOADING`
- `VERIFYING`
- `SUCCESS`
- `FAILED`

记录：

- progress。
- 上传开始输出 `INFO ota upload_start size=<friendly> sha256=<provided|none>`。
- 上传过程中按 10% 阶段输出少量 `INFO ota upload_progress progress=<N>% bytes=<friendly> total=<friendly>`，不按 chunk 刷屏。
- 上传完成输出 `INFO ota upload_success size=<friendly> sha256=<actual>`；失败继续输出 ERROR。
- 成功、失败或 abort 后输出上传摘要，例如 `INFO ota upload_summary duration=13.49s uploaded=1020.2 KB average=75.7 KB/s`。ArduinoOTA/espota 使用 `arduino_upload_summary` / `arduino_upload_failed` 前缀。
- bytesProcessed。
- totalSize。
- totalSize human。
- expectedSha256。
- calculatedSha256。
- lastError。
- startTime。

## 4. 上传流程

流程：

1. 请求认证。
2. 读取总大小和可选 SHA256。
3. 进入 `UPLOADING`。
4. 暂停 Config deferred flush。
5. 如 Watchdog 启用，remove current task。
6. 关闭 WiFi power save 并记录原状态。
7. `Update.begin(...)`。
8. 写入 chunk。
9. 每个 chunk 后 `yield()`。
10. 接收完成后计算 SHA256。
11. SHA256 通过后调用 `Update.end(true)`。
12. 进入 `SUCCESS`。
13. 恢复 Watchdog。
14. 恢复 WiFi power save。
15. 恢复 Config deferred flush。
16. Web 上传页收到成功响应后提示重启，并在短暂等待后跳转到当前配置的首页。
17. 走统一 restart。

失败或 abort 必须恢复：

- Watchdog。
- WiFi power save。
- Config deferred flush。
- Web 上传页留在 OTA 页面，显示失败原因并允许重新上传。
- OTA 状态。

## 5. SHA256

API：

```cpp
bool startUpload(size_t totalSize, const char* expectedSha256Hex = nullptr);
```

Web OTA 可从以下来源读取：

- Header: `X-Sha256`
- Form field: `sha256`

规则：

- 摘要必须是 64 字符 hex。
- 未提供摘要时允许跳过校验。
- 提供摘要时必须校验。
- 校验失败不得调用 `Update.end(true)`。
- 校验失败不得重启到新固件。
- ArduinoOTA/espota 使用 ArduinoOTA 内建 MD5 校验；Web OTA 的 SHA256 规则不强加给 espota。
- 命令行 `webota` 默认自动计算并发送 SHA256，设备端按 Web OTA SHA256 规则校验。

关键顺序：

```text
finish receive -> finish SHA256 -> compare -> Update.end(true) -> restart
```

禁止：

```text
Update.end(true) -> compare SHA256
```

## 6. Watchdog

OTA 写 flash 期间：

```cpp
Esp32BaseWatchdog::removeCurrentTaskForLongOperation();
```

结束后：

```cpp
Esp32BaseWatchdog::restoreCurrentTaskAfterLongOperation();
```

所有路径都必须 restore：

- success
- failed
- abort
- client disconnect

## 7. NVS deferred flush pause

OTA 上传期间不执行普通 deferred flush。

原因：

- OTA 和 NVS 都写 flash。
- 并发写 flash 会导致延迟、卡顿、Web 上传失败。

实现：

- `UPLOADING` 时 `Esp32BaseConfig::pauseDeferredFlush()`。
- `SUCCESS / FAILED / IDLE` 时恢复。
- 统一 restart 前仍执行 `flushAll()`。

## 8. WiFi power save

OTA 期间必须临时关闭 WiFi power save：

1. 记录原 power save 状态。
2. 上传开始前关闭。
3. 上传结束后恢复。

原因：

- modem sleep 会降低吞吐。
- 上传大固件时可能超时。

恢复要求：

- success、failed、abort、client disconnect 全路径恢复原状态。
- 如果关闭或恢复失败，必须记录 warn，但不得破坏 OTA 状态机清理。

## 9. Brownout

默认不关闭 brownout detector。

可选宏：

```cpp
ESP32BASE_OTA_DISABLE_BROWNOUT_DURING_WRITE=0
```

只有用户显式开启时，OTA 写入期间临时关闭 brownout，结束后恢复。

风险：

- 关闭 brownout 可能掩盖供电问题。
- 不建议生产默认开启。

具体实现受 Arduino Core / IDF 版本影响，兼容策略见 [Arduino Core 兼容性](08_arduino_core_compat.md)。

## 10. 回滚

公开 API 在 `Esp32BaseOta`：

```cpp
bool markCurrentValid();
bool isRollbackPossible();
void rollbackAndRestart(const char* reason);
```

策略：

- 新固件启动后，应用自检通过再调用 `markCurrentValid()`。
- 若启用自动 mark valid timeout，`Esp32BaseOta::handle()` 负责检查超时并 rollback。
- timeout 从当前固件 boot 完成后开始计算。
- timeout 状态绑定当前 running partition，避免 rollback 后再次误判形成循环。

宏：

```cpp
ESP32BASE_OTA_REQUIRE_MARK_VALID=0
ESP32BASE_OTA_MARK_VALID_TIMEOUT_MS=30000
```

当 `REQUIRE_MARK_VALID=1` 且超时未标记：

- 写重启日志。
- rollback。
- restart。

## 11. 必测场景

- Web Auth 开启时，未授权上传不调用 `Update.begin()`。
- Web Auth 关闭时，OTA route 仍可访问。
- espota 正确 `--auth` 可上传，错误 `--auth` 失败且设备保持可访问。
- `pio run -t webota` 可通过 IP 地址上传，错误 Web Auth 失败且设备保持可访问。
- Web OTA 与 espota 不得同时写 flash。
- SHA256 错误，上传失败且不切换分区。
- OTA 中途断电 30% / 70% / 99%。
- OTA 后未 mark valid，确认回滚。
- OTA 期间 1Hz NVS 写入，确认 deferred flush pause。
- Watchdog 启用时 OTA 不触发误复位。
- Brownout during OTA 不变砖。
- 上传页进度显示百分比、raw bytes 和 KB/MB。
