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

Esp32Base 同时提供更快的命令行 Web OTA target。它复用现有 HTTP Web OTA 接口和 Web Basic Auth，不依赖 mDNS，推荐直接使用设备 IP：

```ini
extra_scripts =
  post:path/to/Esp32Base/scripts/esp32base_webota.py

custom_esp32base_webota_host = 192.168.2.112
custom_esp32base_webota_user = <current-web-auth-user>
custom_esp32base_webota_password = <current-web-auth-password>
```

运行：

```sh
pio run -t webota
```

`webota` target 由 Esp32Base 提供的可复用 PlatformIO extra_script 注册；业务项目不需要复制脚本或编写自定义 target。可用配置项：

- `esp32base_webota_url`：完整 URL，例如 `http://192.168.2.112/esp32base/ota/raw`。
- `esp32base_webota_host`：设备 IP 或 hostname；未设置 URL 时使用。
- `esp32base_webota_port`：默认 `80`。
- `esp32base_webota_path`：默认 `/esp32base/ota/raw`，走 raw binary endpoint；显式设为 `/esp32base/ota` 时走 multipart 上传路径，和浏览器手工选择文件上传使用同一个设备端 OTA handler。
- `esp32base_webota_user` / `esp32base_webota_password`：当前 Web Auth 用户名和密码；未配置时脚本仍可从环境变量读取，但不再假设设备内置 `admin/admin`。
- `esp32base_webota_auth`：可用 `user:password` 一次性设置 Basic Auth。
- `esp32base_webota_sha256`：默认 `auto`，可设为 `off` 或 64 字符 SHA256。
- `esp32base_webota_timeout`：默认 `120` 秒，用于预检和普通请求。
- `esp32base_webota_upload_timeout`：默认 `90` 秒，用于实际固件上传，弱网或大固件可单独调大。
- `esp32base_webota_chunk_size`：默认 `65536` 字节；脚本默认 raw endpoint 使用 65536 字节分块，在 ESP32_Faucet 1.3MB FULL 固件实测中比 multipart 64KB 略快。未显式配置该项时，raw 上传会根据上传前 3 次 RSSI 取样自动降速：RSSI 低于 -70 dBm 时使用 16 KB 分块，RSSI 低于 -75 dBm 时使用 8 KB 分块。弱网、RSSI 低或仍出现早期 Broken pipe 时可显式降到 32768/16384/8192 后复测。
- `esp32base_webota_raw_pause_ms`：默认 `0`；raw endpoint 每个固件分块发送后暂停的毫秒数。未显式配置该项时，脚本会随弱 RSSI 自动启用短暂停顿；显式配置后以配置值为准。multipart `/esp32base/ota` 不使用该参数。
- `esp32base_webota_socket_send_buffer`：默认 `0`，表示使用操作系统默认 socket 发送缓冲。raw endpoint 可显式设置为较小字节数，例如 4096，用于弱链路下让客户端写入进度更接近设备实际接收速度；脚本仍保持 raw HTTP body 提交，不会在 raw 失败后自动改走 multipart。
- `esp32base_webota_preflight`：默认 `true`，上传前通过状态接口快速校验连接和认证；预检还会比较本地固件大小和设备下一 OTA 分区容量，超出时不会发送固件 body，避免错误密码或固件过大时传完整包。
- `esp32base_webota_status_path`：默认 `/esp32base/api/ota`，用于预检。

配置优先级为当前 `platformio.ini` 环境的 `custom_esp32base_webota_*` 配置、`[esp32base_webota]` 公共段、环境变量。环境变量名使用配置项大写形式，例如 `ESP32BASE_WEBOTA_HOST`。

使用边界：

- `espota` 是 ArduinoOTA 标准协议，兼容 PlatformIO/ArduinoOTA 常规工具链。
- `webota` 是 Esp32Base 提供的 HTTP 上传方式，默认 64 KB 分块并使用 raw endpoint；要求设备 Web 服务和 `/esp32base/ota/raw` 可访问。
- 浏览器 Web OTA 页面仍使用 `/esp32base/ota` multipart 表单上传路径，脚本默认值不会改变手工选择文件上传的设备端 handler。
- `webota` 命令行日志会显示友好容量单位、开始时间、结束时间、上传前 3 次 RSSI 取样、完成时 RSSI、客户端写入 socket 用时、等待设备响应用时、端到端用时和平均速度，并按约 5% 粒度输出固定字段进度。上传进度表示客户端已写入 socket 的字节数，不等同于设备已完成 flash 写入。
- raw Web OTA 的设备端接收由 Arduino ESP32 `WebServer` 的 `HTTPRaw` 小 buffer 驱动，写入 flash 期间会自然形成 backpressure。脚本端大分块只改变从固件文件读取的粒度，不能扩大设备端接收 buffer。为避免 `HTTPRaw` 最后一包短读等待超时，脚本会把 raw HTTP `Content-Length` 补齐到 Arduino raw buffer 的整数倍，并用 `X-Firmware-Size` 声明真实固件大小；设备端只写入真实固件字节，忽略传输 padding。脚本不把每次 socket send 强行切成 1436 字节；1436 只用于传输总长度 padding。raw 请求头会和第一段固件 body 在同一次 socket 写中发送，避免部分网络栈上 header 已到达但第一段 body 迟迟没有进入设备端 raw 读取循环，最终表现为早期客户端中止。Esp32Base 的 WebServer 包装层会接管 raw body 读取：不再使用 Arduino `Stream::readBytes(HTTP_RAW_BUFLEN)` 等满 1436 字节，而是按 `client.available()` 读取当前可用 TCP 数据；连接仍在但暂时无数据时会短暂等待、yield/喂狗，并由 `ESP32BASE_WEB_RAW_NO_PROGRESS_TIMEOUT_MS` 控制无进展上限。普通 header/plain/form 解析仍保留 Arduino WebServer 语义，并通过 `ESP32BASE_WEB_REQUEST_READ_TIMEOUT_SEC` 保持有限读超时。设备端 raw handler 会在上传期间动态申请 64KB 聚合缓冲，先聚合多个 `HTTPRaw` 小块，再按 4KB flash 写入块交给 OTA 层，减少每 1436 字节触发一次 flash write 的阻塞和超时风险，同时在 raw body 接收和长 flash 写期间持续喂狗/yield。raw abort 诊断中的 `written` 表示已写入 OTA 层的字节，`buffered` 表示聚合缓冲中尚未 flush 的字节，`raw` 表示 raw body parser 已接收的 body 字节和声明的 raw `Content-Length`。现场若看到客户端 Broken pipe 且设备 `/esp32base/api/ota` 的 `lastError` 类似 `client aborted raw upload endpoint=/esp32base/ota/raw written=<n> buffered=<n> raw=<n>/<n> progress=<n>%`，先确认使用的是最新脚本和固件；弱网下仍可在 raw endpoint 下把 `esp32base_webota_chunk_size` 调到 32768/16384/8192，设置 `esp32base_webota_raw_pause_ms`，或设置较小 `esp32base_webota_socket_send_buffer` 后复测。默认 raw 命令不会在 raw 失败后自动改走 multipart。
- raw padding 依赖当前 Arduino ESP32 `WebServer` 的 `HTTP_RAW_BUFLEN=1436`。该值和 raw parser 行为已按源码核对 Arduino ESP32 `2.0.16`、`2.0.17`、`3.0.0`、`3.0.7`、`3.3.0`；其中 `2.0.x`/`3.0.x` 仍固定按 raw buffer 读取，`3.3.0` 已按剩余长度读取，继续 padding 仍安全。升级 Arduino ESP32 core 或替换 HTTP server 实现时，必须重新核对该常量和 raw parser 是否仍按固定 buffer 读取；如果常量变化，需要同步更新 `scripts/esp32base_webota.py` 的 `HTTP_RAW_BUFLEN`、相关检查脚本和本节说明，否则 raw 默认 64 KB 分块可能重新出现最后短包等待或 raw upload aborted。本项目暂不在上传脚本中动态解析 Arduino core 头文件，避免为该边界引入额外运行时复杂度。该风险只影响 raw endpoint；浏览器表单上传和显式 `/esp32base/ota` multipart 路径不依赖 `HTTPRaw`，但 `webota` 默认 raw 命令不会在失败后自动切换到 multipart。raw 上传失败不会完成 OTA boot 分区切换，设备应保持原固件运行；若 Web 服务仍可访问，可由操作者明确选择表单或显式 multipart 路径远程恢复。
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

启用 OTA 的固件启动时会输出一条 INFO 级 `boot_summary`，包含 `running_partition`、`configured_boot_partition`、`next_update_partition`、分区地址和当前 ELF SHA256。排查“烧录后仍运行旧程序”时，先看 `running_partition` 是否仍是旧槽，再看 `configured_boot_partition` 是否指向预期槽；两者不一致时日志会额外输出 `boot_partition_mismatch` 警告。

OTA 上传过程中会输出 INFO 级阶段日志：`upload_start stage=begin` 表示已选择目标槽并开始写入，`upload_progress stage=write` 按 10% 粒度输出写入进度，`upload_stage stage=verify` 表示开始校验 SHA/大小，`upload_stage stage=set_boot` 表示开始完成镜像并切换 boot 分区，`upload_stage stage=boot_confirmed` 表示已读取并确认 bootloader 下次启动槽。失败日志统一带 `stage`、`error`、`target`、`address` 和 `progress`，用于判断失败发生在写入、校验还是启动槽切换阶段。

推荐恢复方式是使用基础库脚本写入两个 OTA app 槽，并清除 `otadata`：

```sh
python path/to/Esp32Base/scripts/esp32base_serial_recover_ota.py \
  -d path/to/business/project \
  -e esp32dev \
  --port /dev/ttyUSB0
```

脚本会先构建目标 PlatformIO env，读取 `board_build.partitions` 对应 CSV，定位 `data/ota`、`ota_0`、`ota_1`，写入 bootloader、partition table 和当前 `firmware.bin`，默认把同一固件写入所有 OTA app 槽。`otadata` 清理不会再作为第二条 `erase_region` 命令执行；脚本会生成一个与 `data/ota` 分区等长的全 `0xFF` 临时镜像，并随其他镜像一起放进同一次 `write_flash`，避免写完 app 槽后因板子未重新进入下载模式而清理失败。`boot_app0` 默认跟随 `data/ota` 偏移；清理 `otadata` 时脚本会跳过 `boot_app0`，以清理结果为准，避免同一条命令重复写同一地址或写到 `ota_0` 前的无语义空洞。恢复前可先加 `--dry-run` 查看完整 `esptool.py` 命令、写入槽位和 `otadata` 处理方式；特殊板型可用 `--bootloader-offset`、`--partition-offset`、`--boot-app0-offset` 显式覆盖偏移。

恢复脚本独占串口。执行前先关闭 `pio device monitor`、IDE 串口监视器和其他占用同一 `/dev/cu.*` 或 `/dev/tty.*` 端口的程序；脚本在 macOS/Linux 上会尽量用 `lsof` 提前报告占用者。部分 CH340/CP210x 板子在 460800 下可能进入 stub 后传输中断，可用 `--baud 115200` 降速恢复；如果仍停在 `Connecting...`，需要按住 BOOT 并复位，让设备重新进入下载模式。

如果只想手动恢复，原则是写入当前启动槽或同时写入两个 OTA 槽，并清除 `otadata`。例如 ESP32_Faucet 的 4MB 双 OTA 分区表中 `otadata=0x19000`、`ota_0=0x20000`、`ota_1=0x180000`，恢复命令应至少覆盖当前实际启动槽；最稳妥是同一份 `firmware.bin` 同时写入 `0x20000` 和 `0x180000`，并在同一次 `write_flash` 中把 0x2000 字节全 `0xFF` 镜像写到 `0x19000`。如果把 `erase_region 0x19000 0x2000` 放到第二条 esptool 命令执行，部分板子需要再次手工按 BOOT 进入下载模式，否则可能出现两个 OTA 槽都写入成功但 `otadata` 未清理的恢复假象。

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
- 若启用自动 mark valid timeout，`Esp32BaseOta::handle()` 负责检查超时并 rollback。
- timeout 从 OTA boot 状态初始化完成后开始计算，不依赖 WiFi 连接或 Web 服务启动。
- timeout 状态绑定当前 running partition，避免 rollback 后再次误判形成循环。
- setup/应用初始化阶段崩溃应由 bootloader rollback 处理；固件能运行但一直未确认时，由 `Esp32BaseOta::handle()` 的 timeout 触发 rollback。
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

ESP32_Faucet 这类量产项目的最小 `platformio.ini` 配置：

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
- 启用 `ESP32BASE_OTA_REQUIRE_MARK_VALID=1` 时，即使 WiFi/Web 未 ready，OTA boot/rollback 状态也已初始化，`handle()` 能执行 mark-valid timeout 检查。
- 双 OTA 串口恢复脚本 dry-run 输出两个 OTA app 槽写入，并显示 `otadata` 会在同一次 `write_flash` 中写入全 `0xFF` 镜像完成清理。
- 双 OTA 串口恢复脚本 dry-run 覆盖 hex、K/M 和空 offset 分区表，确认能解析 `otadata`、`ota_0`、`ota_1`。
- 双 OTA 串口恢复前关闭串口 monitor；测试覆盖端口占用诊断，实机恢复失败时优先排查 monitor 占用、下载模式进入和高波特率链路稳定性。
- SHA256 错误，上传失败且不切换分区。
- OTA 中途断电 30% / 70% / 99%。
- OTA 后未 mark valid，确认回滚。
- OTA 期间 1Hz NVS 写入，确认 deferred flush pause。
- Watchdog 启用时 OTA 不触发误复位。
- Brownout during OTA 不变砖。
- 上传页进度显示百分比、已上传容量和总容量；页面只显示 KB/MB/B 人性化值，状态/API JSON 保留 raw `bytes` 字段。
