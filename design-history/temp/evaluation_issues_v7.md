# Esp32Base 全面评估审核报告（第七轮）

> 评估日期：2026-05-07
> 评估范围：src/ 全部源码 + docs/ 全部文档 + library.json + README.md
> 评估原则：只读评估，未做任何修改

---

## 一、第六轮问题修复确认

| # | 原问题 | 修复状态 | 修复方式 |
|---|--------|----------|----------|
| 1 | OTA upload currentSize 高风险 | ✅ 不成立 | Arduino ESP32 WebServer 的 `currentSize` 表示当前 buffer 中的数据大小，`totalSize` 才是累计值。当前代码 `Esp32BaseOta::writeChunk(upload.buf, upload.currentSize)` 正确 |
| 2 | `restart()` 中冗余 FileLog flush | ✅ 已修复 | `Esp32BaseSystem.cpp:103-112` 只保留一次 `FileLog::flush()`（107-109 行），移除了 LOG_W 之后的第二次 flush |
| 3 | `deepSleepUs()` 中冗余 FileLog flush | ✅ 已修复 | `Esp32BaseSleep.inc:28-41` 只保留一次 `FileLog::flush()`（32-34 行），移除了 LOG_I 之后的第二次 flush |
| 4 | 文档 begin/handle 顺序未包含 FileLog | ✅ 已修复 | `docs/01_architecture.md:173-187` begin 顺序新增第 6 步 FileLog；`docs/01_architecture.md:214-231` handle 顺序新增第 3 步 FileLog handle |

---

## 二、全面评估

### 2.1 Core 层

#### Esp32BaseLog ✅
- 编译期日志过滤 + 运行时级别 + 自定义 sink
- 新增 `LineSink` 和 `TimeProvider` 接口，支持文件日志和 NTP 时间戳切换
- `message[192]` / `line[256]` 栈缓冲区，长日志会被截断。对嵌入式设备合理

#### Esp32BaseConfig ✅
- 直接使用 NVS C API，完全消除 `String` 堆分配
- `g_stringScratch[4000]` 静态缓冲区复用
- 写前比较，值不变时跳过 NVS 写入
- 审计日志，默认关闭

#### Esp32BaseSystem ✅
- `resetReason()` / `wakeReason()` 完整实现
- `appendRestartLog()` 环形缓冲区（容量 4），持久化到 NVS
- `restart()` 中 `FileLog::flush()` 只调用一次 ✅

### 2.2 Runtime 层

#### Esp32BaseFileLog ✅
- 通过 Core Log 的 `setInternalLineSink` 接收格式化后的日志行
- WARN/ERROR 立即写文件，INFO/DEBUG 使用 1KB/2s 缓存
- 文件轮转：current → .1 → .2 → .3
- 写入失败时优先 truncate 恢复 current 写入能力
- `streamSegment` 使用 128 字节栈缓冲区逐块读取

#### Esp32BaseBus ✅
- 两阶段遍历 + `subscriptionStillActive()` 检查
- callback 中 unsubscribe 安全

#### Esp32BaseWatchdog ✅
- `timeoutMs < 1000U` 拒绝
- Arduino Core 2.x/3.x API 差异处理正确

#### Esp32BaseSleep ✅
- `deepSleepUs()` 中 `FileLog::flush()` 只调用一次 ✅
- ESP32-C3 GPIO wakeup 明确返回 false

#### Esp32BaseFs ✅
- `listDir` 显式 close File 对象
- 新增 `readBytesAt` / `writeBytesAt`

#### Esp32BaseHealth ✅
- `ESP32BASE_HEALTH_LOOP_WARN_MS` 可配置（默认 3000ms）
- loop 超限 WARN，正常 DEBUG

### 2.3 Network 层

#### Esp32BaseWiFi ✅
- 连接超时、断开重连、凭证校验完善
- `connect()` 中凭证保存失败时返回 false

#### Esp32BaseDns ✅
#### Esp32BaseNtp ✅
- `ESP32BASE_NTP_SYNC_MIN_EPOCH` 可配置（默认 1700000000）
- 同步 pending 日志每 30s 输出

#### Esp32BaseMdns ✅
- WiFi 断开时自动 stop

### 2.4 Web 层

#### Esp32BaseWeb ✅
- 导航系统、首页模式、系统导航模式
- 内置页面：Logs、Reboot
- chunked 响应，消除 String 堆分配
- 慢请求检测（>250ms）
- HTML/CSV 转义

### 2.5 Update 层

#### Esp32BaseOta ✅
- SHA256 校验，Core 2.x/3.x 兼容
- `copySha256Hex()` 验证 hex 字符
- `g_otaProcessed != g_otaTotal` 大小校验
- `finishUpload()` 和 `rollbackAndRestart()` flush FileLog
- OTA upload `currentSize` 语义正确 ✅

---

## 三、文档一致性审核

| 文档项 | 状态 |
|--------|------|
| begin 顺序（含 FileLog） | ✅ `docs/01_architecture.md:173-187` |
| handle 顺序（含 FileLog） | ✅ `docs/01_architecture.md:214-231` |
| Runtime 层模块列表（含 FileLog） | ✅ `docs/01_architecture.md:57-77` |
| Profile 依赖（FILELOG→FS） | ✅ `docs/02_profiles.md:70` |
| Profile 表 | ✅ `docs/02_profiles.md:86-97` |

---

## 四、剩余问题

### 高风险（0 个）

无。

### 中风险（0 个）

无。

### 低风险（6 个）

| # | 位置 | 问题 | 说明 |
|---|------|------|------|
| 1 | `Esp32BaseLog.cpp:58,71` | `message[192]` / `line[256]` 栈缓冲区可能截断长日志 | 对嵌入式设备合理 |
| 2 | `Esp32BaseWatchdog.inc:33` | `idle_core_mask = 0` 仅监控 core 0 | ESP-IDF 默认行为 |
| 3 | `Esp32BaseWiFi.inc:33` | 明文密码存储 RAM 中 | 嵌入式常见做法 |
| 4 | `Esp32BaseOta.inc:272` | `g_otaProcessed * 100U` 可能溢出 32 位 | 固件 <2MB 不会触发 |
| 5 | `Esp32BaseFileLog.inc:231` | `writeLineBuffered` 中 `g_fileLogLastFlushMs` 只在 buffer 空时更新 | buffer 满触发 flush 后计时器不重置，但下次 flush 时会正确更新 |
| 6 | `Esp32BaseFileLog.inc:451` | `streamSegment` 使用 128 字节栈缓冲区 | 适合大文件流式输出，无风险 |

---

## 五、质量评分

| 维度 | 评分 (1-10) | 说明 |
|------|------------|------|
| 架构设计 | 9 | 分层清晰，FileLog 通过 sink 接入 Core Log 是优秀设计 |
| 内存安全 | 9 | Config 消除 String 堆分配，Web 改为 chunked 输出 |
| 边界处理 | 9 | 大部分边界检查到位 |
| 并发安全 | 9 | Bus 两阶段遍历修复 callback 中 unsubscribe 问题 |
| 资源泄漏 | 9 | File 句柄显式关闭 |
| 错误处理 | 9 | 关键路径完整，OTA 逻辑正确 |
| 文档一致性 | 9 | 文档与代码高度一致 |
| API 设计 | 9 | Web 层导航/首页模式/慢请求检测实用 |
| 编译安全 | 9 | Profile 依赖检查完善 |

**综合评分：9.0 / 10**

---

## 六、结论

Esp32Base 库质量优秀，第六轮评估中的所有问题均已修复或澄清。

**高风险问题**：0 个
**中风险问题**：0 个
**低风险问题**：6 个（均为可接受的设计选择或边界情况）

### 项目状态

代码已达到高质量发布标准。建议进入 Phase 0 原型验证阶段：
1. 各 profile 编译产物和 map 文件验证
2. 实机 free heap / min heap 记录
3. OTA 断电/回滚测试
4. WiFi 断线重连测试
5. 48 小时 soak 测试
6. FileLog 轮转和写入验证
