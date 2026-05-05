# Esp32Base 文档体系评估报告

**评审视角**：资深嵌入式系统架构师 / ESP32 Arduino Core 库维护者 / 长期量产设备固件负责人 / 技术文档负责人
**评审对象**：`README.md` + `docs/00..09`（共 11 份，约 2171 行）
**对照基线**：`design-history/` 中 V3 蓝图与 V3 review 的 12 项修订
**评审目标**：判断当前文档是否可作为"一次定型、不再大改"的最终实施依据
**评审日期**：2026-05-03

---

## 0. 总体结论

**结论：推荐批准这套文档作为正式实施依据，但必须先修订 3 个 P0 阻塞问题与若干 P1 改进项。**

> V3 评审的 **12 项修订（P0×3 + P1×6 + P2×3）100% 命中**，其中部分写得比 review 建议还细致（例如 `clearLibraryNamespaces` 命名）。
> 新发现的 **3 个 P0 阻塞 + 10 个 P1 + 5 个 P2** 全部是**实现细节、API 完整性或文档表述**问题，**无任何架构层面的问题**。
> 修订后即可冻结进入 Phase 0 原型。

| 维度 | 评分 | 说明 |
|---|---|---|
| 文档体系完整性 | 8.5/10 | 11 份分工得当；缺 known-limitations 与 quickstart |
| 架构合理性 | 9/10 | 五层稳定，依赖单向 |
| Profile 与裁剪 | 9/10 | 7 个 profile 终态，规则清晰 |
| API 契约 | **7.5/10** | Fs/Sleep/Health API 偏薄；deferred 读写语义未明 |
| 可靠性与安全 | 9/10 | OTA 链路、Captive Portal、Watchdog 联动闭环 |
| ESP32 兼容性 | 8.5/10 | 矩阵明确；缺 SHA256 实现细节、S3 USB-CDC 提示 |
| 测试可执行性 | 8.5/10 | Phase 0 与 CI 矩阵分层合理；缺 soak test |
| 文档质量 | 8/10 | 表述精炼；缺图示与反例；几处轻微不一致 |

**一句话判断**：当前文档已比绝大多数开源 ESP32 库的设计文档更完整、更克制。**只需修补 API 完整性与若干表述漏洞**，即可进入 Phase 0。

---

## 1. V3 评审 12 项修订的命中核对

| # | V3 review 问题 | 文档落地位置 | 评价 |
|---|---|---|---|
| P0-A | System / Ota OTA API 重复 | 03_api §5 + 05_ota §10 双向写明"System 不公开 OTA API" | ✅ 完全采纳 |
| P0-B | Aliases `WiFi` 与 Arduino 冲突 | 03_api §1 "禁止 using namespace；别名采用 `Wifi`" | ✅ 完全采纳 |
| P0-C | Log API 锁定 | 03_api §3 完整宏 + Level enum + Sink 类型 | ✅ 完全采纳 |
| P1-D | OTA mark valid timeout 触发位置 | 05_ota §10 "Esp32BaseOta::handle() 负责" | ✅ 完全采纳 |
| P1-E | OTA WiFi power save 切换 | 05_ota §8 完整章 + §4 步 6/14 + 09 §5 | ✅ 完全采纳 |
| P1-F | `Update.end(true)` 在 SHA256 之后 | 05_ota §5 "关键顺序"图 + 09 §5 显式列项 | ✅ 完全采纳 |
| P1-G | Bus task assert | 03_api §6 + 07 §2.4 入队验证 | ✅ 完全采纳 |
| P1-H | Web 路由缓冲机制 | 03_api §9 + 04_web §5 | ✅ 完全采纳 |
| P1-I | Fs maintenance 删除/no-op | 01 §10 末段注明 | ✅ 完全采纳 |
| P2-J | Config clearAll | 03_api §4 用更安全的 `clearLibraryNamespaces()` | ✅ 完全采纳 |
| P2-K | flushOne 命名 | 03_api §4 拆为 `flushNextDue` / `flushNextForced` | ✅ 完全采纳 |
| P2-L | setStr value 长度 | 03_api §4 "string value 不超过 4000 字节" + 06 §4 | ✅ 完全采纳 |

**结论：12 项 100% 命中、无打折。** 这次迭代的工程纪律是优秀级。

---

## 2. 必须修改的阻塞问题（P0）

### P0-1 —— `Esp32BaseFs` API 不完整，发布后必被改

`03_api.md §11` 提供的 Fs API 仅有：

```cpp
writeFile(path, content)   // const char*
readFile(path, out, len)   // char*
removeFile / exists / totalBytes / usedBytes / freeBytes
```

**对一次定型的库这是硬伤**。生产中的 LittleFS 高频用法包括：

- 二进制读写（图标、TLS 证书、传感器 blob、备份配置）
- 列出目录（用于 Web 文件管理或诊断）
- 文件大小查询（流式读取前预分配）
- 追加写入（日志类 use case）

发布后用户提出"我要存一张 ico/上传一份证书"的需求时，库不能用，就必须改 API；改 API 就违背"不再大规模迭代"承诺。**必须在 V1 就补全**：

```cpp
class Esp32BaseFs {
public:
    static bool begin();
    static bool isReady();

    // text
    static bool writeFile(const char* path, const char* content);
    static bool readFile(const char* path, char* out, size_t len);
    static bool appendFile(const char* path, const char* content);

    // binary
    static bool writeBytes(const char* path, const uint8_t* data, size_t len);
    static bool readBytes(const char* path, uint8_t* out, size_t maxLen, size_t* readLen);
    static bool appendBytes(const char* path, const uint8_t* data, size_t len);

    // metadata
    static bool exists(const char* path);
    static bool removeFile(const char* path);
    static bool rename(const char* from, const char* to);
    static int64_t fileSize(const char* path);   // -1 表示不存在

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
```

**修订位置**：`docs/03_api.md §11`。

---

### P0-2 —— `Esp32BaseConfig` deferred 写后立即读的语义未定义

03_api.md §4 提供 `setIntDeferred()` + `getInt()`，但**没说"设置后还没落盘就读，会拿到新值还是旧值"**。这是 deferred write 模型的核心语义，未定义会成为长期 bug 源。

**两种合理选择**：

- **(A) read-through pending（推荐）**：`getInt` 先查 pending 队列，命中则返回 pending value；否则读 NVS。语义直观，符合用户直觉。代价是 get 需要遍历 pending 表（容量 ≤ 8，可忽略）。
- **(B) NVS-only**：`getInt` 始终读 NVS。pending 期间读不到自己刚写的值。**这种语义会把每个用户都坑一次**，强烈不推荐。

**必须在 03_api.md §4 显式选定 (A)，并写在 API 注释中**：

> `getXxx` 读值时优先查 pending 队列，未命中再读 NVS，因此 `setXxxDeferred` 后立即 `getXxx` 总是返回最新值。

同时写明：`pendingCount()` 和 `clearLibraryNamespaces()` 在 OTA UPLOADING 期间的行为（应正常工作，仅 deferred flush 暂停）。

---

### P0-3 —— 缺 `docs/10_known_limitations.md`

V3 review 已建议过。当前各种"边界 / 不做 / 风险"散落在多份文档：

- 00_design §3 不做清单（设计向）
- 04_web §1 / §8 / 09 §10 长 handler 限制
- 05_ota §9 brownout 风险
- 06_memory_budget §5 PSRAM 不依赖
- 07/08/09 散落的告警

**对一次定型的库，"已知限制"必须有专属一份文档**，让用户在选型时一次看全：

```text
docs/10_known_limitations.md

1. Basic Auth 是明文，不抵御 LAN 内嗅探/MITM
2. WebServer 单线程，长 handler 会卡所有请求
3. Bus 不支持跨任务 publish；ISR/LWIP 回调必须入队
4. mDNS 在 Arduino Core 3.x 下 stop/start 行为见 08
5. 不支持 PSRAM 加速
6. 不支持 HTTPS / mTLS
7. NVS 写满后 set 返回 false，库不自动清理
8. LittleFS 默认不格式化，首次空分区会 mount fail
9. 重启日志环对 NVS 寿命的影响（量级估算）
10. ESP32-C3 wake source 受限，部分 GPIO wake API 返回 false
11. OTA 不支持差分升级，只支持整包
12. 不支持 OTA 加密（仅 SHA256 完整性校验）
```

**README §定位 节末**应当增加一行 "已知限制详见 [docs/10_known_limitations.md](docs/10_known_limitations.md)"，并把"文档入口"中的列表加到 11 项。

---

## 3. 强烈建议但不阻塞的问题（P1）

### P1-1 —— 缺 48h soak test

V3 review 提过。一次定型的库**必须做长时间稳定性验证**。建议在 `07_diagnostics.md §4` 末尾追加：

> **48 小时 soak（FULL profile，三芯片各一台）**：业务每秒 1 次 NVS deferred 写、每 10 分钟 1 次 Web 状态查询、每 30s health.tick；监控 min heap、reset count、卡死、自动重连成功率。任何指标退化或意外重启视为不通过。

并在 `09_release_checklist.md §3` 或新增 §12 列入 "soak test 报告已附入 release notes"。

### P1-2 —— `Esp32BaseSleep::deepSleepXxx` 内部走统一流程未在 API 文档写明

01_architecture §11 写了"所有 deep sleep 走 Esp32BaseSleep::deepSleep(...)"，但 03_api §11 的 `deepSleepSeconds`/`deepSleepUs` 注释里**没说**该方法内部会执行 `flushAll → 写日志环 → 输出诊断 → esp_deep_sleep_start`。用户读 API 时会以为只是个 thin wrapper。

**修订**：在 03_api §11 Sleep 类前加一段注释：

> `deepSleepSeconds/Us` 不是 IDF API 的 thin wrapper。内部强制走 §11 生命周期流程：发布事件 → 写重启日志环 → `Config::flushAll()` → 输出诊断 → 处理 watchdog → `esp_deep_sleep_start`。**禁止用户直接调用 `esp_deep_sleep_start()`**。

### P1-3 —— `Esp32Base::setHostname` 持久化策略未定义

03_api §2 提供 `setHostname()`，但是否存 NVS、是否持久化、是否影响 mDNS、必须在何时调用——全部未说。

**建议明确**：

> `setHostname` 仅运行时设置；不写 NVS。应用应在 `Esp32Base::begin()` **之前**调用一次。如果应用希望持久化，应自行通过 `Esp32BaseConfig` 存取并在 begin 前写回。mDNS 启动时读取当前 hostname，因此 setHostname 必须在 mDNS 启动（即 WiFi connected）之前。

### P1-4 —— `Esp32BaseWiFi::clearCredentials()` 后续行为未定义

03_api §7 提供该 API 但没说后续状态机走向。建议明确：

> `clearCredentials` 清空 NVS 中库管理的 WiFi 凭证，**不立即触发重连或 portal**。下次 `begin()`（或显式 `disconnect()`+`reconnect()`）才进入 CONFIG_PORTAL。

### P1-5 —— 库 NVS namespace 命名约定未公开

`clearLibraryNamespaces()` 不能误删业务 namespace，前提是库用前缀。但前缀是什么没说。**建议在 03_api.md §4 注明**：

> 库内部使用的 NVS namespace 全部以 `eb_` 前缀（例如 `eb_wifi`、`eb_sys`、`eb_log`）。**业务必须避免使用 `eb_` 前缀**。`clearLibraryNamespaces()` 只清前缀匹配的 namespace。

### P1-6 —— mDNS 降级方案应在 Phase 0 后定型，文档去掉 if-else

08_arduino_core_compat §6 写"如果 stop/start 不稳定，断网时只标记 not advertising"。这个"如果"对一次定型的库不应保留——Phase 0 原型 2.2 必须给出确定结论。

**建议**：Phase 0 之后修订 08 §6 为唯一方案（推荐"始终用 not-advertising 软停"，这是 IDF 社区已广泛使用的稳定方案）。

### P1-7 —— `Esp32BaseHealth` API 与诊断字段不对齐

07_diagnostics §6 列了 Health 记录 7 项指标（uptime/heap/min heap/loop max period/reset reason/WiFi/FS）。但 03_api §11 的 Health API 只暴露 `lastTickMs()` 和 `loopPeriodMaxMs()`。其他 5 项要么补 API，要么明确"通过对应模块直接查（System/WiFi/Fs）"。

**推荐后者**（更简洁）。03_api §11 Health 类前加一段：

> Health 仅负责**周期性采样和发布 health.tick 事件**。其他指标（heap、reset reason、WiFi state、FS state）请直接通过对应模块的查询 API 获取。Health 不重复包装这些值。

### P1-8 —— brownout 关闭的具体实现未说

05_ota §9 提供 `ESP32BASE_OTA_DISABLE_BROWNOUT_DURING_WRITE` 但没说**怎么关**。Arduino-ESP32 没有官方 disable API，需要直接 `WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)` 之类操作。Core 2.x / 3.x 下寄存器名可能不同。

**建议**：08_arduino_core_compat §10（新增）"brownout 控制"，明确两个版本下的写法。

### P1-9 —— OTA 事件常量未在 03_api §10 列出（与 WiFi 对齐）

03_api §7 WiFi 类导出 `EVENT_CONNECTED` 等事件常量。但 §10 OTA 类没列 `EVENT_START / EVENT_PROGRESS / EVENT_SUCCESS / EVENT_FAILED` 常量定义。Web、Health 同问题。

**修订**：所有发布事件的模块都必须在自己的类中导出 `static constexpr const char* EVENT_*` 常量。在 03_api §6 Bus 类前加全局规则；并在 §9 §10 §11.Health 各加上对应常量。

### P1-10 —— `Esp32BaseDns` 是否允许扩展拦截目标必须明确

04_web §3 列了 3 个内置探测域名。但 Dns API 没有 `addCaptiveTarget`。是硬编码到底，还是可扩展？

对中国大陆使用场景，可能需要拦截 `connect.rom.miui.com` / `wifi.vivo.com` 之类。一次定型必须在 V1 决定：

- **方案 A（推荐）**：通配 DNS 全部解析到 AP IP（不需要域名白名单），所有 OS 探测请求都打到 AP。
- **方案 B**：硬编码列表，明确"不可扩展"。

**建议方案 A**，并在 04_web §3 写明："Dns 模块对所有 DNS 查询返回 AP IP，无需维护域名白名单。内置 captive 探测路径（`/generate_204`、`/hotspot-detect.html` 等）由 Web 层返回触发弹出的响应。"

如果选方案 B，需在 03_api §8 加 `bool addCaptiveTarget(const char* domain);`。

---

## 4. 文档之间的不一致点

| # | 不一致 | 修订 |
|---|---|---|
| C1 | 02_profiles §12 与 06_memory_budget §2 表格内容相同但措辞略异 | 02 引用 06 或反之，避免双源 |
| C2 | 03_api §6 "严格模式" 与 01_architecture §9 `STRICT_OPTIONAL_BEGIN` 同名易混 | Bus 用 `ESP32BASE_BUS_STRICT_TASK_CHECK` 命名 |
| C3 | 05_ota §4 步 6/14（power save）与 §8 章节重复 | §8 改为 "详见 §4 步 6/14"，避免维护双份 |
| C4 | 07_diagnostics §6 Health 字段 vs 03_api §11 Health API 字段不对齐 | 见 P1-7 |
| C5 | 04_web §3 Captive 域名列表 vs 03_api §8 Dns API 不可扩展 | 见 P1-10 |
| C6 | 01_architecture §10 handle 无 "Fs maintenance"，与 V3 蓝图一致；但 03_api §11 Fs 也无 maintenance；两者口径一致，但需要在 01 §10 末段补一行 "本设计有意省略 Fs handle 调用" | 加注释避免读者怀疑漏写 |
| C7 | README profile 表与 02_profiles §4 内容一致但 README 简化了 Bus 列；表中 RUNTIME 在 README 写 "yes/no/no/no..."，未列 Bus 列，可能让用户以为 RUNTIME 没 Bus | README 表加 Bus 列或注释 "Bus 在 RUNTIME 默认开启" |

---

## 5. 可能过度设计的地方

总体非常克制，**只有 1 处轻微过度**：

- **`ESP32BASE_OTA_REQUIRE_MARK_VALID` + `MARK_VALID_TIMEOUT_MS`**（05_ota §10）：自动 mark valid timeout 在第一版引入是合理的，但要写清楚"默认关闭"且"FULL profile 也不默认开"。如果默认开会导致用户的应用还没自检完就被 rollback，反而引起问题。**确认 05_ota §10 已说默认 0，OK**，无需改动。

无其他过度设计。设计纪律到位（明确不做清单、明确发布冻结）。

---

## 6. 可能遗漏的关键设计或测试

| # | 遗漏 | 建议 |
|---|---|---|
| M1 | **OTA SHA256 流式实现细节**：Arduino-ESP32 没有原生流式 SHA256 API，必须用 `mbedtls_sha256_*`，需要在每个 chunk 后 update | 08 加一节 "SHA256 实现"；Phase 0 原型 3 必须验证 |
| M2 | **ESP32-S3 USB-CDC 串口**：S3 默认 USB-CDC 而非 UART0；Log 默认目标必须明确 | 03_api §3 + 08 加一行说明 |
| M3 | **NVS partition 最小尺寸假设** | 06 §6 应注明"建议 NVS ≥ 24KB"，并附推荐 partitions.csv 模板路径 |
| M4 | **应用 NVS 与库 NVS 隔离规则** | 见 P1-5 |
| M5 | **CONFIG_PORTAL → STA 的状态切换时序** | 04_web 加 "状态切换原子性" 一节：停 DNS → Web 不停 → 启动 NTP → 启动 mDNS |
| M6 | **WiFi 状态机 FAILED 与 RETRY_BACKOFF 区分条件、用户恢复路径** | 03_api §7 注释或 04_web 补充 |
| M7 | **回归测试机制**：发布后修 bug 时如何保证不破坏其他功能 | 09 加一节 "维护期变更必须重跑完整 CI + 实机验收清单" |
| M8 | **资源记录表 §7 全空** | 06 §7 加注释 "在 Phase 1 完成后填入 CORE 数据；release 前必须填全所有 cell" |
| M9 | **Esp32Base::lastError() 的语义**：03_api §2 提供但没说什么时候被设置/清除 | 加注释或建议删除（用 begin 失败策略 + 日志即可） |
| M10 | **Esp32BaseLog::Sink callback 的线程安全性** | 03_api §3 注明 "Sink 可能从多个任务/ISR 调用？" 或限定 "Sink 仅从调用 LOG 宏的当前任务调用" |

---

## 7. 对 README 的改进建议

README 已经短小精悍（79 行），但**缺 3 件东西**：

### R1 —— 加 "5 分钟上手"

放在 "## Profiles" 之前，让新用户立刻有可执行起点：

```markdown
## 5 分钟上手

```cpp
// platformio.ini
build_flags =
    -DESP32BASE_PROFILE_NET_RUNTIME

// main.cpp
#include <Esp32Base.h>

void setup() {
    Esp32Base::setFirmwareInfo("MyDevice", "1.0.0");
    Esp32Base::begin();
}
void loop() { Esp32Base::handle(); }
```

打开浏览器访问 `http://<device-ip>/esp32base` 查看状态。
```

### R2 —— 加"已知限制"链接

在"定位"或"不支持"清单后引导用户阅读 `docs/10_known_limitations.md`。

### R3 —— Profile 表加 Bus 列或注释

当前表 5 列（Core/Runtime/Network/Web/OTA）没显示 Bus 状态。读者会以为 RUNTIME 没 Bus（实际有）。要么加 Bus 列，要么在表下加一句 "Bus 默认在所有非 CORE/NET profile 中开启；NET 默认关、可显式启用"。

### R4 —— 加版本和支持状态徽章

虽然第一版未发布，但 README 应预留位置：

```markdown
[![Arduino Core](https://img.shields.io/badge/arduino--esp32-2.0.14%2B%20%7C%203.0.4%2B-blue)]
[![Boards](https://img.shields.io/badge/boards-ESP32%20%7C%20S3%20%7C%20C3-green)]
[![Status](https://img.shields.io/badge/status-design--review-yellow)]
```

### R5 —— "最终实施原则"段提到 design-history

当前 README 只字未提 `design-history/`。读者会困惑为何有这么多 V1/V2/V3 review。应加一行：

> 设计演进过程见 `design-history/`，仅作为背景参考；正式实现以 `docs/` 为准。

---

## 8. 逐文件评审

### 8.1 README.md（79 行）

- ✅ 定位清晰，目标和不支持列表对称。
- ✅ Profile 表一目了然。
- ⚠️ 缺 quickstart（R1）、known limitations 链接（R2）、Bus 列说明（R3）、design-history 说明（R5）。
- 建议长度上限 150 行。

### 8.2 docs/00_design.md（148 行）

- ✅ 设计原则克制，发布冻结条款（§2.5）有纪律。
- ✅ 明确不做清单足够明确。
- ⚠️ §4.1 资源目标列了 4 项验收，但和 06_memory_budget §2 重复。建议 00 里只留"高层目标"，细则放 06。
- ⚠️ §5 设计冻结条件 5 项可作为 release gate，建议在 09 中显式引用。

### 8.3 docs/01_architecture.md（274 行）

- ✅ 五层 + begin/handle 顺序 + 生命周期，结构最完整。
- ✅ §10 末段已声明 "LittleFS 当前没有 maintenance 任务"，符合 V3 review P1-I 修订。
- ⚠️ §9 严格模式 vs §6（Bus）的"严格模式"重名（C2）。
- ⚠️ §10 handle 顺序与 V3 蓝图 §6.3 略有调整（删 Fs maintenance），需在文末加一句"本设计有意省略，与 V3 蓝图差异详见 design-history"。
- ⚠️ §11 生命周期与 03_api §11 Sleep API 应交叉引用（P1-2）。

### 8.4 docs/02_profiles.md（238 行）

- ✅ 7 个 profile 终态。
- ✅ 展开契约写到了 `#ifndef`（V3 review P2 完整采纳）。
- ⚠️ §3 软依赖没列 OTA 是 Bus 的软依赖。建议补：`OTA` 可不依赖 Bus；未启用 Bus 时不发布 OTA 事件。
- ⚠️ §1 底层宏列表没标注"profile 宏可二选一使用"，需要小段说明。
- ⚠️ §10 WEB_RUNTIME 中关 Bus 的影响未说（P1）。

### 8.5 docs/03_api.md（446 行）

- ✅ 公开 API 已锁定。
- ✅ Log API 完整。
- ✅ Bus task assert、flushNextDue/Forced、clearLibraryNamespaces、Wifi 别名、Aliases 警告全部到位。
- ⚠️ Fs API 严重不完整（P0-1）。
- ⚠️ Config deferred 读取语义未定义（P0-2）。
- ⚠️ Sleep deepSleep 内部走统一流程未注明（P1-2）。
- ⚠️ setHostname 持久化策略（P1-3）。
- ⚠️ clearCredentials 后续行为（P1-4）。
- ⚠️ 库 NVS 前缀（P1-5）。
- ⚠️ Health API 与诊断对齐（P1-7）。
- ⚠️ OTA / Web / Health 事件常量未列（P1-9）。
- ⚠️ Aliases 完整映射表未列（建议附）。
- ⚠️ Esp32Base::lastError 语义未定义（M9）。
- ⚠️ Log::Sink 线程安全性未注（M10）。

**这是修订工作量最大的文档**，但全部是补漏不是重写。

### 8.6 docs/04_web.md（168 行）

- ✅ 启动条件清晰。
- ✅ Captive Portal 多设备测试明确。
- ✅ 路由缓冲机制完整。
- ✅ Handler 性能边界已写为"已知限制"。
- ⚠️ §3 拦截域名列表 vs Dns API 是否可扩展冲突（P1-10）。
- ⚠️ 缺"状态切换原子性"一节（M5）。
- ⚠️ 缺一个最小路由示例（用户友好）。

### 8.7 docs/05_ota.md（219 行）

- ✅ OTA 链路完整：认证、SHA256 顺序、Watchdog、NVS pause、power save、rollback、brownout 全覆盖。
- ✅ §5 关键顺序图清晰。
- ✅ §11 必测场景与 09 §5 一致。
- ⚠️ §4 与 §8 内容重复（C3）。
- ⚠️ §10 自动 mark valid timeout 的 timeout 起点未明确（建议加一句"timeout 从 system boot 起算"）。
- ⚠️ §10 rollback 后的循环风险未提（旧固件再 timeout 会怎样？）。

### 8.8 docs/06_memory_budget.md（149 行）

- ✅ 容量与字符串限制完整。
- ✅ 分区建议提到 4MB / 8MB 差异。
- ⚠️ §2 与 02_profiles §12 重复（C1）。
- ⚠️ §6 缺推荐 partitions.csv 模板路径与 NVS 最小尺寸假设（M3）。
- ⚠️ §7 资源表全空（M8）。
- ⚠️ §3.5 重启日志环 NVS 寿命影响未量化估算（建议加：约每条 64B × 4 条 = 256B；每天 1 次重启 × 10 年 ≈ 3650 次擦写，well within NVS lifetime）。

### 8.9 docs/07_diagnostics.md（189 行）

- ✅ Phase 0 五个原型分工明确。
- ✅ CI 矩阵分 PR / release，工程合理。
- ✅ 实机验收清单全面。
- ⚠️ 缺 48h soak（P1-1）。
- ⚠️ 缺回归测试机制（M7）。
- ⚠️ §6 Health 字段与 API 对齐（P1-7 / C4）。
- ⚠️ §3 "主流 Arduino Core 3.x" 应锁定具体版本范围。

### 8.10 docs/08_arduino_core_compat.md（123 行）

- ✅ Watchdog / WiFi event / Sleep / mDNS 差异列出。
- ⚠️ §6 mDNS 降级"如果"未定型（P1-6）。
- ⚠️ 缺 SHA256 实现细节（M1）。
- ⚠️ 缺 ESP32-S3 USB-CDC 提示（M2）。
- ⚠️ 缺 brownout 寄存器跨版本差异（P1-8）。
- ⚠️ §7 "Update 差异" 一行带过过浅，建议拓展到 5~10 行（含 SHA256 / partition 校验 / abort 行为）。

### 8.11 docs/09_release_checklist.md（138 行）

- ✅ 11 节覆盖文档/架构/编译/裁剪/OTA/配网/存储/FS/Watchdog/Web/维护规则。
- ✅ §11 维护期规则与"一次定型"承诺一致。
- ⚠️ 缺 soak test 项（P1-1）。
- ⚠️ 缺回归测试规则（M7）。
- ⚠️ 缺一项 "已知限制文档已发布"（依赖 P0-3）。
- ⚠️ 应显式引用 00_design §5 的设计冻结条件，作为 release gate 一致性确认。

---

## 9. 推荐的最终修订工作量估算

| 文档 | 修订量 | 工时 |
|---|---|---|
| README.md | 加 quickstart / 链接 / Bus 列说明 / design-history 说明 | 0.5h |
| 00_design.md | 与 06 去重 | 0.2h |
| 01_architecture.md | 严格模式更名 / handle 注释 | 0.3h |
| 02_profiles.md | 软依赖补 OTA / WEB_RUNTIME 关 Bus 影响 | 0.3h |
| **03_api.md** | **Fs API 补全 / deferred 语义 / setHostname / clearCred / NVS 前缀 / Sleep 注释 / Health 对齐 / 事件常量 / Aliases 表 / lastError / Sink** | **3h** |
| 04_web.md | 状态切换原子性 / DNS 通配方案 / 加示例 | 0.5h |
| 05_ota.md | §4/§8 去重 / timeout 起点 / 循环风险 | 0.5h |
| 06_memory_budget.md | 与 02 去重 / 资源表说明 / NVS 大小 / 寿命估算 | 0.5h |
| 07_diagnostics.md | 加 soak / 回归 / Core 版本锁定 / Health 对齐 | 0.5h |
| 08_arduino_core_compat.md | mDNS 定型 / SHA256 实现 / S3 USB-CDC / brownout / Update 拓展 | 1h |
| 09_release_checklist.md | 加 soak / 回归 / known limitations 项 / 引用 00 §5 | 0.3h |
| **10_known_limitations.md** | **新建** | **1h** |
| **总计** | | **~8.5 小时** |

约 **1 个工作日**即可完成全部修订。

---

## 10. 推荐最终模块顺序、Profile 集合、实施阶段

**模块顺序**：与文档完全一致，**无需改动**。

```text
Layer 0  Core      = Log + Config + System
Layer 1  Runtime   = Bus + Watchdog + Sleep + Fs + Health
Layer 2  Network   = WiFi + Dns + Ntp + Mdns
Layer 3  Web       = Web + (WebStatus / WebWiFi / WebUtil 内部)
Layer 4  Update    = Ota + (WebOta 内部)
入口             = Esp32Base
```

**Profile 集合**：与文档完全一致，**7 个不再扩张**。

**实施阶段**：与 09_release_checklist 和 design-history 一致，**6.5~7 周**。

---

## 11. 发布前必须通过的实机验证清单（汇总）

打 ✅ 是 07/09 已列；打 ➕ 是必须新增。

- ✅ 7 个 profile × 3 芯片 × 2 Arduino Core 编译矩阵
- ✅ CORE / NET / WEB 裁剪验收（map 无重符号）
- ✅ OTA 默认认证拒绝 / SHA256 错误拒绝 / Update.end 顺序
- ✅ OTA 中途断电 30/70/99% 不变砖 / 后未 markValid 可回滚
- ✅ OTA 期间 1Hz NVS 写入（pause 生效）
- ✅ Captive Portal iPhone/Android/macOS/Windows
- ✅ 路由器掉电 5 分钟恢复
- ✅ NVS 写满 / LittleFS 首次挂载失败不 halt
- ✅ JSON escape / ESP32-C3 单核重负载
- ✅ mDNS 多次重连恢复（验证 Phase 0 选定方案）
- ✅ deferred write + restart 全部落盘
- ✅ brownout during OTA 不变砖
- ➕ **48 小时 soak test（FULL profile，三芯片各一台）**
- ➕ **二进制文件 IO**（如 P0-1 修订后的 Fs API 补完）
- ➕ **deferred set 后立即 read**（验证 P0-2 的 read-through 语义）
- ➕ **OTA + WiFi power save**（验证 §8 切换正确）
- ➕ **OTA mark valid timeout 自动回滚**（设 require=1 + 30s timeout，故意不调 markValid）
- ➕ **Bus 跨任务 publish 拒绝**（在 LWIP 任务调用 publish，应拒绝并记日志）
- ➕ **Esp32-S3 USB-CDC 日志输出**（确认 Log 默认目标在 S3 板上正常）
- ➕ **跨 Arduino Core 升级回归**（2.0.14 全套 → 升 3.0.4 重测）
- ➕ **库 NVS 与业务 NVS 隔离**（`clearLibraryNamespaces` 不删业务数据）

---

## 12. 是否仍需替代架构？

**明确不需要**。理由与 V3 review 一致：

1. 三轮迭代（V1 → V2 → V3 → 文档）已收敛到稳定结构。
2. 当前所有问题都是**实现细节、API 完整性、文档表述**，**架构 0 改动**。
3. 任何替代方向（trait/policy 模板、静态注册表）都已论证过弊大于利。
4. 一次定型的库**最怕在最后阶段推翻架构**。

**唯一应做的事**：把本评审的 P0×3 + P1×10 + 文档不一致 + 缺漏项落实为修订（约 1 个工作日），然后冻结进入 Phase 0。

---

## 13. 一句话总结

> **当前文档体系已经达到可作为正式实施依据的成熟度，强烈推荐批准。**
> **进入 Phase 0 前必修 3 个 P0**（`Esp32BaseFs` API 补全、Config deferred 读取语义、新增 known-limitations 文档）；
> **完成约 8.5 小时（1 个工作日）的细节修订后，可直接冻结进入 Phase 0 原型阶段；**
> **修订无需改动任何架构决策，全部是补漏与表述细化。**
