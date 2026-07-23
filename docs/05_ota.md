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

OTA route 按 profile/OTA 编译条件注册；Web OTA 认证来自当前 Web Auth。未设置应用默认认证且没有已保存认证时，Web 服务不会启动，OTA HTTP 路由也不会开放。

- Web Auth 开启时，OTA 页面和上传接口复用 Web Basic Auth。
- Web Auth 关闭时，OTA 页面和上传接口不要求认证，风险由应用和用户自行承担。

`ESP32BASE_ENABLE_ARDUINO_OTA` 默认跟随 `ESP32BASE_ENABLE_OTA`。启用 OTA 的 profile 默认同时支持 PlatformIO/espota：

```ini
upload_protocol = espota
upload_port = <hostname>.local
upload_flags =
  --auth=<password>
```

命令行 OTA 使用 `Esp32Base::hostname()`、标准端口 3232 和现有 mDNS host 记录；ArduinoOTA 自身不重复启动 mDNS。hostname 默认值来自 `ESP32BASE_DEFAULT_HOSTNAME`，Web/API 保存的 hostname 需要重启后才会被 DHCP hostname、mDNS 和 ArduinoOTA 使用。业务不需要命令行 OTA 时，可显式 `-D ESP32BASE_ENABLE_ARDUINO_OTA=0`。

espota 认证密码为当前生效的 Web Auth 密码；espota 没有用户名，因此 Web 用户名不参与。即使 Web Auth 被关闭，ArduinoOTA 仍要求密码，避免静默开放无密码 OTA。

Esp32Base 同时提供更快的命令行 Web OTA target。它复用现有 HTTP Web OTA 接口和 Web Basic Auth。`esp32base_webota_host` 同时接受设备 IP、普通 DNS hostname 和 mDNS `<hostname>.local`；DHCP 地址可能变化的本地开发设备推荐使用当前 Esp32Base hostname 对应的 `.local` 地址：

```ini
extra_scripts =
  post:path/to/Esp32Base/scripts/esp32base_webota.py

custom_esp32base_webota_host = esp32base-full.local
custom_esp32base_webota_user = <current-web-auth-user>
custom_esp32base_webota_password = <current-web-auth-password>
```

运行：

```sh
pio run -t webota
```

`webota` target 由 Esp32Base 提供的可复用 PlatformIO extra_script 注册；业务项目不需要复制脚本或编写自定义 target。可用配置项：

- `esp32base_webota_url`：完整 URL，例如 `http://192.168.2.112/esp32base/ota/raw`。
- `esp32base_webota_host`：设备 IP、普通 DNS hostname 或 mDNS `<hostname>.local`；未设置 URL 时使用。不要给普通 DNS hostname 自动追加 `.local`。
- `esp32base_webota_port`：默认 `80`。
- `esp32base_webota_path`：默认 `/esp32base/ota/raw`，走 raw binary endpoint；显式设为 `/esp32base/ota` 时走 multipart 上传路径，和浏览器手工选择文件上传使用同一个设备端 OTA handler。
- `esp32base_webota_user` / `esp32base_webota_password`：当前 Web Auth 用户名和密码；未配置时脚本仍可从环境变量读取，但不再假设设备内置 `admin/admin`。
- `esp32base_webota_auth`：可用 `user:password` 设置 Basic Auth。
- `esp32base_webota_sha256`：默认 `auto`，可设为 `off` 或 64 字符 SHA256。
- `esp32base_webota_timeout`：默认 `120` 秒，用于预检和普通请求。
- `esp32base_webota_upload_timeout`：默认 `90` 秒，用于实际固件上传，弱网或大固件可单独调大。
- `esp32base_webota_chunk_size`：默认 `65536` 字节。未显式配置该项时，raw 上传会根据上传前 3 次 RSSI 取样自动降速：RSSI 低于 -70 dBm 时使用 16 KB 分块，RSSI 低于 -75 dBm 时使用 8 KB 分块。弱网或 RSSI 低时可显式降到 32768/16384/8192 后复测。
- `esp32base_webota_raw_pause_ms`：默认 `0`；raw endpoint 每个固件分块发送后暂停的毫秒数。未显式配置该项时，脚本会随弱 RSSI 自动启用短暂停顿；显式配置后以配置值为准。multipart `/esp32base/ota` 不使用该参数。
- `esp32base_webota_socket_send_buffer`：默认 `0`，表示使用操作系统默认 socket 发送缓冲。raw endpoint 可显式设置为较小字节数，例如 4096，用于弱链路下让客户端写入进度更接近设备实际接收速度；脚本仍保持 raw HTTP body 提交，不会在 raw 失败后自动改走 multipart。
- `esp32base_webota_preflight`：默认 `true`，上传前通过状态接口快速校验连接和认证；预检还会比较本地固件大小和设备下一 OTA 分区容量，超出时不会发送固件 body，避免错误密码或固件过大时传完整包。
- `esp32base_webota_status_path`：默认 `/esp32base/api/ota`，用于预检。

配置优先级为当前 `platformio.ini` 环境的 `custom_esp32base_webota_*` 配置、`[esp32base_webota]` 公共段、环境变量。环境变量名使用配置项大写形式，例如 `ESP32BASE_WEBOTA_HOST`。

使用边界：

- `espota` 是 ArduinoOTA 标准协议，兼容 PlatformIO/ArduinoOTA 常规工具链。
- `webota` 是 Esp32Base 提供的 HTTP 上传方式，默认 64 KB 分块并使用 raw endpoint；要求设备 Web 服务和 `/esp32base/ota/raw` 可访问。
- `webota` 每次执行时通过运行脚本的操作系统解析一次 hostname，并在上传前显示解析到的地址；本次任务的预检、状态采样和上传连接复用该地址，HTTP `Host` 和 HTTPS SNI 仍保留原 hostname。解析结果不会跨任务缓存，因此设备在两次任务之间取得新的 DHCP 地址后，下次执行会重新解析。`.local` 依赖电脑与设备位于允许组播的同一网络，并依赖当前 Windows、macOS 或 Linux 解析环境支持 mDNS；解析失败时脚本会在发送固件前停止并给出对应提示。WSL、跨 VLAN、访客 WiFi 或禁用组播的网络不能假定 `.local` 可用，此时应使用网络 DNS 中的稳定 hostname 或设备 IP。
- 浏览器 Web OTA 页面仍使用 `/esp32base/ota` multipart 表单上传路径，脚本默认值不会改变手工选择文件上传的设备端 handler。
- `webota` 命令行日志会显示容量、耗时、RSSI、速度和阶段进度。上传进度表示客户端已写入 socket 的字节数，不等同于设备已完成 flash 写入。
- raw Web OTA 使用 `X-Firmware-Size` 声明真实固件大小；脚本可为 raw HTTP 传输补齐 padding，设备端只写入真实固件字节。设备端把 `HTTPRaw` 接收块直接交给 OTA 层，Arduino `Update` 在内部按 Flash sector 聚合和写入；raw handler 不再额外申请大块聚合缓冲，因此不会因无法取得连续 64 KB heap 而中止 OTA。该 padding 和接收策略是 raw endpoint 的兼容细节，不影响浏览器 multipart 上传路径。
- raw endpoint 上传失败不会完成 OTA boot 分区切换，设备应保持原固件运行。若 Web 服务仍可访问，可由操作者明确选择表单或显式 multipart 路径远程恢复。
- 升级 Arduino ESP32 Core 或替换 HTTP server 实现时，应重新验证 raw endpoint、padding、超时和弱网表现；如果 raw 路径不稳定，可先降低 `esp32base_webota_chunk_size`、设置 `esp32base_webota_raw_pause_ms` 或改用 multipart 路径。
- 浏览器 Web OTA 页面会在选择文件后立即显示固件大小，并在发送前用当前 next OTA slot 容量检查所选文件；固件过大时直接在页面提示，不发送上传 body。命令行 `webota` 预检也会读取 `/esp32base/api/ota` 的 `nextUpdatePartition.size.bytes` 并在本地固件超出时停止。
- 设备端会在 Web OTA 开始前检查 `X-Firmware-Size` 声明的固件大小和下一 OTA 分区容量；固件大于可写 app slot 时直接失败。上传过程中实际写入字节数也不得超过声明总大小。
- `Update.begin()` 失败时会把底层 Update 错误字符串写入 `lastError()`，便于区分空间、flash 或 Update 状态问题。
- Web OTA 写入成功后会读取 `esp_ota_get_boot_partition()` 二次确认下一启动分区已经切到本次写入目标；确认失败时返回 OTA 失败，不进入自动重启。
- `/esp32base/api/ota` 返回当前 running partition、configured boot partition、next update partition、app partition 列表、镜像 SHA256、版本、OTA state、本次上传目标、实际计算 SHA256 和最后错误，便于判断设备当前运行的是哪个槽位和哪个固件。
- OTA boot 诊断和 rollback/mark-valid 状态在 `Esp32Base::begin()` 早期初始化，不等待 WiFi/Web ready；ArduinoOTA/espota 网络服务仍在 Web/Auth ready 后启动，确保密码来源正确。

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
- 上传开始日志应包含固件大小和是否提供 SHA256。
- 上传过程中按阶段输出少量进度日志，包含百分比和已写入/总大小，不按 chunk 刷屏。
- 上传完成日志应包含实际固件大小和校验结果；失败日志应能区分写入、校验和启动槽切换阶段。
- 成功、失败或 abort 后输出上传摘要，包含耗时、上传字节数和平均速度；ArduinoOTA/espota 也应输出对应摘要。
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
5. 如 Watchdog 启用，进入长操作作用域，保持当前 task WDT 注册。
6. 关闭 WiFi power save 并记录原状态。
7. `Update.begin(...)`。
8. 写入 chunk。
9. 每个 chunk 后 `feed/yield`，避免 WebServer 同步上传期间触发 task WDT。
10. 接收完成后计算 SHA256。
11. SHA256 通过后调用 `Update.end(true)`。
12. 进入 `SUCCESS`。
13. 退出长操作作用域，恢复常规 Watchdog feed 节奏。
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

## 6. 双 OTA 串口恢复

双 OTA 设备通过 Web OTA 成功启动过 `ota_1` 后，`otadata` 可能仍指向 `ota_1`。此时普通串口 `pio run -t upload` 是否会重新选择 `ota_0` 取决于 flash plan：仓库标准分区 `otadata=0xe000`，PlatformIO/Arduino 串口上传会同时写 `0xe000 boot_app0.bin` 和 `0x10000 firmware.bin`，通常会把 OTA data 初始化回 `ota_0`；但业务自定义分区如果把 `otadata` 移到其他偏移，串口上传可能只覆盖固定 app 槽而不更新当前启动槽，设备仍可能继续从 `ota_1` 启动旧固件。串口日志中的 `ELF file SHA256` 也会继续显示旧固件值。

启用 OTA 的固件启动时应输出当前运行槽、配置启动槽、下一更新槽、分区地址和当前镜像 SHA256，便于排查“烧录后仍运行旧程序”。上传过程应输出低频阶段日志，失败时能区分写入、校验和启动槽切换阶段。

推荐恢复方式是使用基础库脚本写入两个 OTA app 槽，并清除 `otadata`：

```sh
python path/to/Esp32Base/scripts/esp32base_serial_recover_ota.py \
  -d path/to/business/project \
  -e esp32dev \
  --port /dev/ttyUSB0
```

脚本会先构建目标 PlatformIO env，读取 `board_build.partitions` 对应 CSV，定位 OTA data 和两个 OTA app 槽，写入 bootloader、partition table 和当前 `firmware.bin`，默认把同一固件写入所有 OTA app 槽并清理启动选择。恢复前可先加 `--dry-run` 查看完整 `esptool.py` 命令、写入槽位和 `otadata` 处理方式；特殊板型可用 `--bootloader-offset`、`--partition-offset`、`--boot-app0-offset` 显式覆盖偏移。

恢复脚本独占串口。执行前先关闭 `pio device monitor`、IDE 串口监视器和其他占用同一 `/dev/cu.*` 或 `/dev/tty.*` 端口的程序；脚本在 macOS/Linux 上会尽量用 `lsof` 提前报告占用者。部分 CH340/CP210x 板子在 460800 下可能进入 stub 后传输中断，可用 `--baud 115200` 降速恢复；如果仍停在 `Connecting...`，需要按住 BOOT 并复位，让设备重新进入下载模式。

如果只想手动恢复，原则是写入当前启动槽或同时写入两个 OTA 槽，并清除 `otadata`。自定义分区项目应先读取实际分区表，确认 `otadata`、`ota_0` 和 `ota_1` 偏移，再生成对应 esptool 命令。最稳妥的方式是在同一次 `write_flash` 中写入需要覆盖的 app 槽，并把与 `otadata` 分区等长的全 `0xFF` 镜像写到 `otadata` 偏移，避免第二条命令执行前设备退出下载模式造成恢复假象。

## 7. Watchdog

OTA 写 flash 期间：

```cpp
Esp32BaseWatchdog::enterLongOperation();
```

结束后：

```cpp
Esp32BaseWatchdog::exitLongOperation();
```

所有路径都必须 exit：

- success
- failed
- abort
- client disconnect

## 8. NVS deferred flush pause

OTA 上传期间不执行普通 deferred flush。

原因：

- OTA 和 NVS 都写 flash。
- 并发写 flash 会导致延迟、卡顿、Web 上传失败。

实现：

- `UPLOADING` 时 `Esp32BaseConfig::pauseDeferredFlush()`。
- `SUCCESS / FAILED / IDLE` 时恢复。
- 统一 restart 前仍执行 `flushAll()`。

## 9. WiFi power save

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

## 10. Brownout

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

## 11. 回滚

公开 API 在 `Esp32BaseOta`：

```cpp
bool markCurrentValid();
bool isRollbackPossible();
void rollbackAndRestart(const char* reason);
```

策略：

- Arduino ESP32 core 在 rollback 启用时提供 weak `verifyOta()` / `verifyRollbackLater()`；默认 `verifyRollbackLater()` 为 `false` 且 `verifyOta()` 为 `true`，新 OTA 镜像可能在应用自检前被 core 过早标记为 valid。
- 量产项目应启用 `ESP32BASE_OTA_REQUIRE_MARK_VALID=1`，由 Esp32Base 覆盖 `verifyRollbackLater()` 并阻止 Arduino core 自动 mark valid。
- 新固件启动后，应用自检通过再调用 `markCurrentValid()`。
- `markCurrentValid()` 只在当前 running partition 为 `pending_verify` 时报告成功；如果已经是 `valid`、`aborted`、`invalid`、`undefined` 或状态不可读，会写日志并返回 `false`。
- 启用确认 timeout 后，rollback hook 在 Arduino `setup()` 前为 `pending_verify` 镜像启动一次性系统计时器；主路径不依赖业务进入 `Esp32Base::begin()`、loop 或调用 `Esp32BaseOta::handle()`。
- timeout 从 rollback hook 识别到 `pending_verify` 开始，不依赖 WiFi 连接、Web 服务或业务初始化。`Esp32BaseOta::begin()` 会确认保护已建立；系统计时器创建失败时记录 ERROR，并由 `handle()` 保留合作式降级检查。
- timeout 状态绑定当前 running partition，避免 rollback 后再次误判形成循环。
- setup/应用初始化阶段崩溃或意外复位由 bootloader rollback；setup 阻塞、提前退出或业务 loop 未调用基础库时，由独立确认期限触发 rollback。关中断、调度器失效等连系统计时器也无法运行的硬卡死仍需硬件 Watchdog 产生复位，再由 bootloader 回滚。
- 业务必须把所有必需的配置/API 注册返回值、存储、传感器、执行器安全状态和核心任务纳入健康检查；基础库不能根据某个业务 API 的失败替应用判断镜像是否可用。
- 启动日志会输出 running partition、configured boot partition、next update partition、当前 OTA image state，以及是否正在等待业务 mark valid。
- `/esp32base/api/ota` 返回 `runningOtaState`、`waitingForMarkValid`、`markValidElapsedMs`、`markValidTimeoutMs`，同时在 `runningPartition.state` 中保留当前 running partition 的 OTA state。

宏：

```cpp
ESP32BASE_OTA_REQUIRE_MARK_VALID=0
ESP32BASE_OTA_MARK_VALID_TIMEOUT_MS=30000
```

当 `REQUIRE_MARK_VALID=1` 且超时未标记：

- 写重启日志。
- rollback。
- restart。

业务最小接入示例：

```cpp
#include <Esp32Base.h>

void setup() {
    Esp32Base::begin();

    const bool appOk = initSensors() && loadBusinessConfig() && startApplication();
    if (appOk && Esp32BaseOta::waitingForMarkValid()) {
        Esp32BaseOta::markCurrentValid();
    }
}

void loop() {
    Esp32Base::handle();
}
```

量产项目的最小 `platformio.ini` 配置：

```ini
build_flags =
  -D ESP32BASE_PROFILE=ESP32BASE_PROFILE_FULL
  -D ESP32BASE_OTA_REQUIRE_MARK_VALID=1
  -D ESP32BASE_OTA_MARK_VALID_TIMEOUT_MS=30000
```

## 12. 必测场景

- Web Auth 开启时，未授权上传不调用 `Update.begin()`。
- Web Auth 关闭时，OTA route 仍可访问。
- espota 正确 `--auth` 可上传，错误 `--auth` 失败且设备保持可访问。
- `pio run -t webota` 可通过 IP 地址上传，错误 Web Auth 失败且设备保持可访问。
- Web OTA 与 espota 不得同时写 flash。
- Web OTA 成功后 `/esp32base/api/ota` 中 `bootPartition` 与 `lastTargetPartition` 一致。
- 启用 `ESP32BASE_OTA_REQUIRE_MARK_VALID=1` 时，即使未进入 `Esp32Base::begin()`、WiFi/Web 未 ready、setup 阻塞或业务未调用 `handle()`，确认期限仍能把未确认镜像标记无效并重启回滚。
- 模拟系统计时器创建失败时，启动日志输出 ERROR，`handle()` 降级检查仍能在期限后回滚。
- 回滚到上一镜像后，启动日志能根据上一槽的 `invalid` / `aborted` state 输出拒绝原因分类和分区信息。
- 双 OTA 串口恢复脚本 dry-run 输出两个 OTA app 槽写入，并显示 OTA data 清理计划。
- 双 OTA 串口恢复脚本 dry-run 覆盖 hex、K/M 和空 offset 分区表，确认能解析 `otadata`、`ota_0`、`ota_1`。
- 双 OTA 串口恢复前关闭串口 monitor；测试覆盖端口占用诊断，实机恢复失败时优先排查 monitor 占用、下载模式进入和高波特率链路稳定性。
- SHA256 错误，上传失败且不切换分区。
- OTA 中途断电 30% / 70% / 99%。
- OTA 后未 mark valid，确认回滚。
- OTA 期间 1Hz NVS 写入，确认 deferred flush pause。
- Watchdog 启用时 OTA 不触发误复位。
- Brownout during OTA 不变砖。
- 上传页进度显示百分比、已上传容量和总容量；页面只显示 KB/MB/B 人性化值，状态/API JSON 保留 raw `bytes` 字段。
