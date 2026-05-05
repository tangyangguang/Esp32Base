# OTA 与回滚

## 1. 定位

OTA 是 FULL profile 的核心能力，必须按量产可靠性设计。

OTA 不只是 Web 上传文件，还包括：

- 认证。
- SHA256。
- Watchdog 联动。
- Config deferred flush pause。
- rollback。
- 中途断电恢复。

## 2. 启动条件

OTA 依赖：

- WiFi connected。
- Web ready。
- Web Auth enabled。
- 应用显式调用过 `Esp32BaseWeb::setAuth()`。

如果未显式设置 auth：

- OTA route 不注册。
- 输出 error。
- `/esp32base/ota` 不可用。

OTA 上传页不再叠加第二套认证，只复用 Web Basic Auth。

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
16. 走统一 restart。

失败或 abort 必须恢复：

- Watchdog。
- WiFi power save。
- Config deferred flush。
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

- 未授权上传，不调用 `Update.begin()`。
- 未调用 `setAuth()`，OTA route 不注册。
- SHA256 错误，上传失败且不切换分区。
- OTA 中途断电 30% / 70% / 99%。
- OTA 后未 mark valid，确认回滚。
- OTA 期间 1Hz NVS 写入，确认 deferred flush pause。
- Watchdog 启用时 OTA 不触发误复位。
- Brownout during OTA 不变砖。
- 上传页进度显示百分比、raw bytes 和 KB/MB。
