# Esp32Base 全面评估审核报告

> 评估日期：2026-05-07
> 评估范围：src/ 全部源码 + docs/ 全部文档 + library.json + README.md
> 评估原则：只读评估，未做任何修改

---

## 一、项目概览

`Esp32Base` 是面向 ESP32/ESP32-S3/ESP32-C3 的轻量级 Arduino 基础库，采用 5 层架构（Core → Runtime → Network → Web → Update），通过 profile 机制实现编译期裁剪。本轮评估覆盖新增的 `Esp32BaseFileLog` 模块和 Web 层大幅增强。

---

## 二、模块级审核

### 2.1 Core 层

#### Esp32BaseLog ✅
- 新增 `LineSink` 和 `TimeProvider` 接口，支持文件日志和 NTP 时间戳切换
- `message[192]` / `line[256]` 栈缓冲区，长日志会被截断。对嵌入式设备合理。
- 编译期过滤 + 运行时级别 + 自定义 sink，设计完整

#### Esp32BaseConfig ✅
- 直接使用 NVS C API（`nvs_open`/`nvs_get_str`/`nvs_close`），完全消除 `String` 堆分配
- `g_stringScratch[4000]` 静态缓冲区复用
- 写前比较（`setStr`/`setInt`/`setBool`），值不变时跳过 NVS 写入
- 审计日志（`enableConfigAudit`/`enableConfigReadAudit`），默认关闭
- `readStoredString` 中 `required > len` 时返回 false 但不设置 `out[0]`，调用方应检查返回值

#### Esp32BaseSystem ✅
- `resetReason()` / `wakeReason()` 完整实现
- `appendRestartLog()` 环形缓冲区（容量 4），持久化到 NVS
- `restart()` 中 `Esp32BaseFileLog::flush()` 被调用两次（107 行和 111 行），中间夹了 `LOG_W`。第一次 flush 后日志已落盘，第二次是冗余操作

### 2.2 Runtime 层

#### Esp32BaseFileLog（新增模块）✅
- 通过 Core Log 的 `setInternalLineSink` 接收格式化后的日志行
- WARN/ERROR 立即写文件，INFO/DEBUG 使用 1KB/2s 缓存
- 文件轮转：current → .1 → .2 → .3
- 写入失败时优先 truncate 恢复 current 写入能力
- `streamSegment` 使用 128 字节栈缓冲区逐块读取，适合大文件流式输出
- `writeLineBuffered` 中 `g_fileLogLastFlushMs` 只在 `g_fileLogBufferUsed == 0` 时更新，如果 buffer 满触发 flush 后不会重置计时器，但下次 flush 时会正确更新
- `writeBytes` 中 `g_fileLogCurrentBytes + len + 1` 可能溢出（len 为 SIZE_MAX 时），但实际 line 长度受 log message 192 字节限制，不会触发

#### Esp32BaseBus ✅
- 两阶段遍历：先复制目标到 `targets[]` 数组，再遍历执行 callback
- `subscriptionStillActive()` 检查确保 callback 中 unsubscribe 不会导致遍历异常
- 设计正确

#### Esp32BaseWatchdog ✅
- `timeoutMs < 1000U` 拒绝并打 WARN 日志
- Arduino Core 2.x/3.x `esp_task_wdt_init` API 差异处理正确
- `idle_core_mask = 0` 仅监控 core 0，ESP-IDF 默认行为

#### Esp32BaseSleep ✅
- `deepSleepUs()` 中 `Esp32BaseFileLog::flush()` 被调用两次（32 行和 36 行），与 `Esp32BaseSystem::restart()` 同样的冗余问题
- ESP32-C3 GPIO wakeup 明确返回 false 并打日志

#### Esp32BaseFs ✅
- `listDir` 中显式 `file.close()` 后再 `file = root.openNextFile()`，最后 `root.close()`
- 所有操作增加 `g_fsReady` 前置检查
- 新增 `readBytesAt` / `writeBytesAt` 支持偏移读写

#### Esp32BaseHealth ✅
- 新增 `ESP32BASE_HEALTH_LOOP_WARN_MS`（默认 3000ms）
- `g_healthTickLoopPeriodMaxMs` 每个 tick 窗口清零，`g_healthLoopPeriodMaxMs` 永不重置
- loop 超限时输出 WARN `loop_slow`，正常时输出 DEBUG `tick`

### 2.3 Network 层

#### Esp32BaseWiFi ✅
- 新增 `ESP32BASE_WIFI_CONNECT_TIMEOUT_MS`（默认 15s）
- 新增 `EVENT_DISCONNECTED` 事件
- 新增 `credentialsValid()` 长度检查
- 连接超时、断开重连、凭证校验完善
- `connect()` 中凭证保存失败时返回 false

#### Esp32BaseDns ✅
- Captive Portal DNS 实现正确

#### Esp32BaseNtp ✅
- `ESP32BASE_NTP_SYNC_MIN_EPOCH` 宏可配置（默认 1700000000，约 2023-11-14）
- 同步 pending 日志每 30s 输出一次
- NTP 同步后切换到绝对时间戳（`Esp32BaseLog::setTimeProvider`）

#### Esp32BaseMdns ✅
- WiFi 断开时自动 stop mDNS（`Esp32Base.cpp:112-114`）
- 启动/停止/服务注册均有日志

### 2.4 Web 层

#### Esp32BaseWeb（大幅增强）✅
- 新增导航系统（`addNavItem`）、首页模式（`HomeMode`）、系统导航模式（`SystemNavMode`）
- 新增内置页面：Logs、Reboot
- 新增 chunked 响应（`beginResponse`/`sendChunk`/`endResponse`）
- 新增 HTML/CSV 转义
- 新增慢请求检测（>250ms 记录 method/uri/elapsed）
- 新增请求上下文管理（`g_requestContextActive`/`g_responseActive`）
- `beginJson`/`endJson` 改为 chunked 输出，消除 String 堆分配
- OTA upload handler 中 `upload.currentSize` 在 UPLOAD_FILE_WRITE 阶段是累计值，传给 `Esp32BaseOta::writeChunk()` 作为 len 参数。`Esp32BaseOta::writeChunk` 内部执行 `Update.write(data, len)` 和 `g_otaProcessed += len`。如果 `upload.currentSize` 是累计值而非本次 chunk 大小，会导致重复写入和进度计算错误

### 2.5 Update 层

#### Esp32BaseOta ✅
- SHA256 校验完整，Core 2.x/3.x 兼容性通过 `sha256Starts()` 处理
- `copySha256Hex()` 验证 hex 字符并转小写
- `g_otaProcessed != g_otaTotal` 大小校验
- `finishUpload()` 和 `rollbackAndRestart()` 中 flush FileLog
- `rollbackAndRestart()` 先 `appendRestartLog` + `flushAll` + `FileLog::flush` 再 rollback

---

## 三、文档一致性审核

### 3.1 begin 顺序

**文档** `docs/01_architecture.md:175-186`：
```
1.Log → 2.Config → 3.System → 4.Bus → 5.Fs → 6.Watchdog → 7.Sleep → 8.Health → 9.WiFi → 10.标记 ready
```

**代码** `src/Esp32Base.cpp:30-67`：
```
Log → Config → System → Bus → Fs → FileLog → Watchdog → Sleep → Health → WiFi → 标记 ready
```

**差异**：代码中 `FileLog` 在 `Fs` 之后、`Watchdog` 之前初始化，文档未提及 FileLog。

### 3.2 handle 顺序

**文档** `docs/01_architecture.md:215-229` 列出的 handle 顺序不包含 FileLog。

**代码** `src/Esp32Base.cpp:73-138` 在 Config handle 之后、WiFi handle 之前有 `Esp32BaseFileLog::handle()`。

**差异**：文档未更新 FileLog 的 handle 位置。

### 3.3 Runtime 层模块列表

**文档** `docs/01_architecture.md:57-64` 已包含 `Esp32BaseFileLog`。✅

### 3.4 Profile 依赖

**文档** `docs/02_profiles.md:70` 已包含 `FILELOG 需要 FS`。✅

---

## 四、问题汇总

### 高风险（1 个）

| # | 位置 | 问题 | 影响 |
|---|------|------|------|
| 1 | `Esp32BaseWeb.inc:732` | OTA upload handler 中 `upload.currentSize` 在 UPLOAD_FILE_WRITE 阶段是累计已接收字节数，而非本次 chunk 大小。传给 `Esp32BaseOta::writeChunk(upload.buf, upload.currentSize)` 会导致 `Update.write` 写入重复数据，`g_otaProcessed` 累加错误 | OTA 固件损坏，设备变砖 |

### 中风险（3 个）

| # | 位置 | 问题 | 影响 |
|---|------|------|------|
| 2 | `Esp32BaseSystem.cpp:107,111` | `restart()` 中 `Esp32BaseFileLog::flush()` 被调用两次，中间夹了一条 `LOG_W` | 冗余操作，不影响功能但浪费 Flash 写入 |
| 3 | `Esp32BaseSleep.inc:32,36` | `deepSleepUs()` 中 `Esp32BaseFileLog::flush()` 被调用两次 | 同上 |
| 4 | `docs/01_architecture.md` | begin/handle 顺序文档未包含 FileLog | 文档与代码不一致 |

### 低风险（6 个）

| # | 位置 | 问题 | 说明 |
|---|------|------|------|
| 5 | `Esp32BaseLog.cpp:58,71` | `message[192]` / `line[256]` 栈缓冲区可能截断长日志 | 对嵌入式设备合理 |
| 6 | `Esp32BaseWatchdog.inc:33` | `idle_core_mask = 0` 仅监控 core 0 | ESP-IDF 默认行为 |
| 7 | `Esp32BaseWiFi.inc:33` | 明文密码存储 RAM 中 | 嵌入式常见做法 |
| 8 | `Esp32BaseOta.inc:272` | `g_otaProcessed * 100U` 可能溢出 32 位 | 固件 <2MB 不会触发 |
| 9 | `Esp32BaseFileLog.inc:231` | `writeLineBuffered` 中 `g_fileLogLastFlushMs` 只在 buffer 空时更新 | buffer 满触发 flush 后计时器不重置，但下次 flush 时会正确更新 |
| 10 | `Esp32BaseFileLog.inc:451` | `streamSegment` 使用 128 字节栈缓冲区 | 适合大文件流式输出，无风险 |

---

## 五、质量评分

| 维度 | 评分 (1-10) | 说明 |
|------|------------|------|
| 架构设计 | 9 | 分层清晰，FileLog 通过 sink 接入 Core Log 是优秀设计 |
| 内存安全 | 8 | Config 消除 String 堆分配，Web 改为 chunked 输出 |
| 边界处理 | 8 | 大部分边界检查到位 |
| 并发安全 | 9 | Bus 两阶段遍历修复 callback 中 unsubscribe 问题 |
| 资源泄漏 | 9 | File 句柄显式关闭 |
| 错误处理 | 8 | 关键路径完整，OTA writeChunk 存在逻辑错误 |
| 文档一致性 | 7 | FileLog 新增后文档未完全同步 |
| API 设计 | 9 | Web 层导航/首页模式/慢请求检测实用 |
| 编译安全 | 9 | Profile 依赖检查完善 |

**综合评分：8.6 / 10**

---

## 六、结论

Esp32Base 库整体质量优秀，但存在 1 个高风险问题需要立即修复。

### 必须修复（高优先级）

**OTA writeChunk 累计值问题**（`Esp32BaseWeb.inc:732`）：

Arduino WebServer 的 `upload.currentSize` 在 `UPLOAD_FILE_WRITE` 阶段表示**已接收的总字节数**（累计值），而非本次 chunk 的大小。当前代码：
```cpp
Esp32BaseOta::writeChunk(upload.buf, upload.currentSize);
```

这会导致：
1. 第一次 chunk：写入 512 字节（正确）
2. 第二次 chunk：写入 1024 字节（实际只有 512 字节新数据，但传了累计值 1024）
3. 后续 chunk 持续放大

修复方案：跟踪上一次累计值，计算差值作为本次 chunk 大小：
```cpp
static size_t lastSize = 0;
size_t chunkLen = upload.currentSize - lastSize;
lastSize = upload.currentSize;
Esp32BaseOta::writeChunk(upload.buf, chunkLen);
```

### 建议修复（中优先级）

1. 去除 `restart()` 和 `deepSleepUs()` 中冗余的二次 `FileLog::flush()`
2. 更新 `docs/01_architecture.md` 中 begin/handle 顺序，加入 FileLog

### 项目状态

除 OTA writeChunk 问题外，其余代码质量优秀。FileLog 模块设计合理，Web 层增强实用，Config 消除 String 堆分配显著降低 heap 碎片风险。修复高风险问题后即可进入 Phase 0 原型验证。
