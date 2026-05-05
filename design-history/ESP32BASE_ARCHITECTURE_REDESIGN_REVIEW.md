# Esp32Base 架构重构方案 —— 专业评估报告

**评审视角**：资深嵌入式系统架构师 / ESP32 Arduino Core 库维护者 / 长期量产设备固件负责人
**评审对象**：`ESP32BASE_ARCHITECTURE_REDESIGN_PLAN.md`
**参考材料**：`/Users/tyg/dir/claude_dir/EspBase/`（设计经验来源）
**评审日期**：2026-05-03

---

## 0. 总体结论

**结论：方案大方向正确，强烈推荐采用，但必须做 12 处必改修订后再进入实施。**

| 维度 | 评分 | 说明 |
|---|---|---|
| 架构合理性 | 8 / 10 | 五层划分清晰，但 Core 层偏重，Bus 不应在 Core 默认启用 |
| 资源裁剪能力 | 7 / 10 | profile 思路对，但 5 个预设覆盖不完，需补一个常见组合 |
| ESP32 专用化收益 | 9 / 10 | 放弃跨芯片 HAL 完全合理，对 ESP32-C3 仍需注意单核与无 PSRAM |
| API 与长期维护 | 7 / 10 | 13 个公开类比当前 5 类清晰，但命名过长且缺命名空间收敛 |
| 可靠性与安全 | 6 / 10 | OTA / Watchdog 联动、Captive Portal、Arduino Core 2.x↔3.x 差异未提 |
| 实施风险 | 8 / 10 | 一次性重写在当前代码量下是合理的；建议按层分阶段 PR |

简短判断：**这是当前能想到的最合理结构之一**，比现有 5 类混合实现明显更可维护。但方案在“ESP32 专用化”上还可以更激进（利用 IDF v5 的能力），在“安全/可靠”上还有几个具体盲点。下面逐项展开。

---

## 1. 主要优点

1. **五层依赖单调递增**：Core → Runtime → Network → Web → Update 的层级方向是嵌入式库的经典正确解，杜绝了当前 Network/Web/Runtime 互相吃职责的混乱。
2. **Esp32Base::begin() 不再阻塞**：把 WiFi、NTP、mDNS、Web、OTA 全部丢到 `handle()` 里按依赖延迟启动，是正确做法，避免冷启动卡住主循环。EspBase 的 `beginNetworkModules()` 已经验证过这条路径可行。
3. **职责拆细**：把 LittleFS / Watchdog 从 Runtime 拆出，把 OTA 从 Web 拆出，是必要的。混合类终究会变成“什么都管的上帝类”，这是当前实现的核心问题。
4. **Profile + ENABLE_* 双层**：profile 给典型用户体验，底层宏给高级用户精细裁剪——分层得当。
5. **明确不做的清单（§11）**：写下了不做 MQTT、不做 WebSocket、不做 Scheduler。这是嵌入式库长期维护最关键的纪律。极其加分。
6. **一次性大重写的勇气**：现有代码 2289 行，重写成本可控，迁移历史包袱不值得保留。新架构必须打破旧 API 才能真正干净。
7. **统一 restart / sleep 前 flushAll**：是当前实现明显缺失的可靠性闭环，方案显式补上，正确。

---

## 2. 主要风险（必须正视）

### R1. Core 层把 Bus 默认装入是浪费

`Esp32BaseBus` 在 §2.1 被列入 Core，所有 profile 都启用。但参考 EspBase 实现：

```cpp
static Subscriber _subscribers[EB_BUS_MAX_SUBSCRIBERS];   // 默认 16
struct Subscriber { bool used; uint16_t id; char event[32]; Callback cb; void* ud; };
// 每条 ≈ 48~56 字节 → 16 条 ≈ 800~900 字节静态 RAM
```

`PROFILE_CORE` 的目标场景（只用 Config + 诊断 + 重启）根本没有发布者也没有订阅者，却要付 ~900 B RAM 的静态成本。**Bus 应该下沉到 Runtime 层或独立成 `ESP32BASE_ENABLE_BUS`，不在 CORE 默认开**。

### R2. CORE / RUNTIME / NET / WEB / FULL 五种 profile 漏了最常见组合

真实项目里高频出现的组合：**WiFi + NTP + Watchdog + LittleFS，无 Web**（典型：上传传感器数据到 MQTT 或 HTTP）。当前 profile 表里：

- `NET` = CORE + WiFi + NTP + mDNS（**没有 Watchdog / Sleep / Fs**）
- `RUNTIME` = CORE + Watchdog + Sleep + Fs（**没有 WiFi**）

用户要么自己用底层宏拼，要么被迫升到 `WEB`/`FULL` 拖一堆 Web/OTA 进来。**建议增加 `PROFILE_NET_RUNTIME`（NET + Watchdog + Sleep + Fs）作为第六个预设**，这是嵌入式量产 80% 的形态。

或者更激进：取消 profile 概念，提供 **能力宏 bitmask + 2 个常用 preset（CORE / FULL）**，避免预设组合爆炸。

### R3. WiFi 配置门户启动时序未澄清

§3.2 写道：
> 3. WiFi ready 后启动 Web。
> 6. Web ready + WiFi connected 后启动 OTA。

但配置门户场景下，**WiFi 是 AP 模式（CONFIG_MODE），不是 connected**。EspBase 的实现用的是 `wifiConnected || state == CONFIG_MODE` 才允许启动 Web。新方案必须明确写：**Web 在 `WiFi.connected || WiFi.config_portal` 时都要启动**，否则配网页面永远出不来。

而且 **Captive Portal 需要 DNS 拦截**（DNSServer），方案完全没提。这是 AP 配网的事实标准，不做用户体验会很差。

### R4. ESP32 Arduino Core 2.x ↔ 3.x 兼容性未列入风险

这是 ESP32 生态最大的坑，方案完全没提：

| 模块 | Arduino Core 2.x（IDF v4.4） | Arduino Core 3.x（IDF v5.x） |
|---|---|---|
| `esp_task_wdt_init` | 接收 `(timeout, panic)` | 接收 `esp_task_wdt_config_t*` 结构体，**API 不兼容** |
| `Update.begin/write/end` | 基本一致 | 基本一致，但 partition table 校验更严 |
| `WiFi.onEvent` 事件枚举 | `SYSTEM_EVENT_*` | `ARDUINO_EVENT_*`，常量名换了 |
| `ESPmDNS` | `MDNS.begin` 即可 | 内部走 IDF mDNS 组件，可能链接顺序敏感 |
| `nvs_*` 直接调用 | 可用 | API 略有变化 |
| `esp_sleep_enable_*_wakeup` | 可用 | 可用，但 RTC GPIO 在 C3 上不存在 |

**必须在 §7 加入 §7.8 "Arduino Core 版本支持"**：明确支持版本范围（建议 ≥2.0.14 且 ≥3.0.4），并用 `ESP_ARDUINO_VERSION_MAJOR` 做条件编译。否则用户升级 Arduino Core 之后构建必断。

### R5. OTA 期间 Watchdog 处理不够具体

§7.7 写"上传期间喂狗或临时暂停 watchdog"，但实际 ESP32 OTA 写 flash 单次 `Update.write` 可能阻塞 100~300ms。TWDT 默认 5s 可能不够，IWDT 更短。建议规则：

1. **进入 OTA 写入前**：`esp_task_wdt_delete(NULL)` 把当前任务从 TWDT 摘掉。
2. **写入循环每次结束**：`yield()` 让 LWIP / WebServer 跑一次。
3. **OTA 失败/完成后**：`esp_task_wdt_add(NULL)` 把任务加回去。
4. **brownout detector**：写 flash 时建议 `WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)` 临时关掉，写完恢复。低压设备烧 OTA 时常见 brownout 复位。

方案需要把这些写成具体协议，不能用"喂狗或暂停"含糊带过。

### R6. Bus 的多任务安全语义不明确

ESP32 是双核（C3 单核但仍有 LWIP 任务）。`WiFi.onEvent` 的回调跑在 **LWIP 任务**，不是 `loop()` 任务。如果用户在事件回调里直接 `EsBus.publish()`，就会跨任务调用：

- EspBase 的 `EbBus` 用了 `volatile uint32_t _lockFlag` 自旋锁，能勉强工作但不是真锁。
- 新方案 §6 写"同步发布、不面向 ISR"——但**没说明跨任务安全性**。

**必须明确**两条规则之一：
- (a) 所有 `publish()` 必须在 `loop()` 任务调用，模块内部把 ISR/LWIP 事件先入队，loop 里再 publish；或
- (b) 用 `portMUX_TYPE` + `portENTER_CRITICAL` 做真正的多核锁。

推荐 (a)，更轻、更可预测。

### R7. NVS Preferences 的 namespace/key 长度限制未约束

ESP32 NVS：**namespace ≤ 15 字符、key ≤ 15 字符**（含 `\0` 是 16）。当前 EspBase 的 `EbConfig::validateName` 已经做了这个检查，但新方案 §7.6 没提。

如果用户写 `setStr("user_settings", "wifi_password_main", ...)`，会静默失败或被 NVS 截断。**`Esp32BaseConfig` 必须强制校验长度并返回 false**，并在编译期对常量字符串做 `static_assert`。

### R8. LittleFS 首次挂载策略需要更细

§7.5 说默认 `ESP32BASE_FS_AUTO_FORMAT=0`。这导致全新设备第一次烧录 → LittleFS 分区为空 → `LittleFS.begin(false)` 失败 → 用户被迫开 auto-format。

正确策略应该是：
1. **第一次 begin**：`LittleFS.begin(false)` 尝试挂载。
2. **挂载失败 + 用户显式 `ESP32BASE_FS_AUTO_FORMAT=1`**：格式化。
3. **挂载失败 + 没开 auto-format**：返回错误，记日志，但**不 halt**（其他功能能照常用）。
4. **首次成功挂载后**：在 NVS 里写一个 `fs.formatted=1` 标志，避免运行中误格式化。

方案要把这套写进 §7.5。

### R9. ESP32-C3 缺少 PSRAM 与 RTC GPIO 的差异未处理

ESP32-C3 单核 RISC-V，**没有 PSRAM、没有 RTC GPIO（不能 ext0/ext1 唤醒任意脚）**。方案声称"对 ESP32 / ESP32-S3 / ESP32-C3 都是一等目标"，但：

- `Esp32BaseSleep` 的 `wakeOnPin` API 在 C3 上要么不存在，要么只能用 GPIO0~5 的特殊唤醒源。
- Web/OTA 大缓冲若想用 PSRAM，必须 `#if CONFIG_SPIRAM_SUPPORT && defined(BOARD_HAS_PSRAM)` 才能 `ps_malloc()`。
- 默认缓冲尺寸应当按芯片 differentiate：`#if CONFIG_IDF_TARGET_ESP32C3` 时把 `Web routes / pages / pending writes` 调小。

方案需要在 §3 增加芯片差异化默认值的明确策略。

### R10. Esp32Base 仍可能演变为"过重的中心化控制器"

§3.2 列了 11 步固定顺序的 `handle()`。**每加一个新模块，都要改 `Esp32Base.cpp`**——这正是当前实现的痛点之一。

可选改良方案（按推荐度排序）：

- **(推荐) 静态注册表**：每个模块在 `.cpp` 里用 `__attribute__((constructor))` 或 `static` 对象的 ctor 把自己注册到 `Esp32Base` 的内部表（带优先级和依赖前提）。`begin()` / `handle()` 遍历表。**好处：新模块零修改 Esp32Base.cpp。坏处：static init order 要小心（避免 SIOF）。**
- **次选** 保持当前硬编码顺序，但在 `Esp32Base.cpp` 里只做 `#ifdef ESP32BASE_ENABLE_*` 块包裹的调用，每块 5 行。13 个模块 = ~65 行，仍可读。**这是我推荐的工程现实选择**，简单可控。

新方案目前是"次选"做法，是 OK 的，但**需在文档里明确：新增模块需要修改 Esp32Base.cpp，这是设计取舍**。

### R11. Web Basic Auth 在 HTTP 上是明文，方案没提示

`WebServer` 的 `authenticate()` 走 HTTP Basic Auth，**密码 base64 即在 LAN 上可被嗅探**。方案 §2.4 / §7.7 强调 OTA 必须经过 Web Auth，但没说"这只挡住意外操作，不防主动攻击"。

应在文档里明确：
1. Basic Auth 仅作"防意外误操作"，**不是安全边界**。
2. **OTA 端点建议默认开启 + 强制要求设置非默认密码**。例如 `setAuth()` 没被调用过就拒绝启动 OTA 路由，启动时输出告警。
3. （可选）支持 `WebServer::authenticateDigest()`，HTTP Digest 可以避免明文密码传输。

### R12. 没有任何 "OTA 失败回滚" 设计

ESP32 双 OTA 分区天然支持回滚 (`esp_ota_mark_app_invalid_rollback_and_reboot`)，但需要应用配合：

- **新固件第一次启动时**：必须在某个时间窗内调用 `esp_ota_mark_app_valid_cancel_rollback()` 标记成功，否则下次启动 bootloader 会自动回滚。
- 方案完全没提。建议 `Esp32BaseOta` 提供 `markCurrentValid()` 与可选的 "boot 后 N 秒未标记则回滚" 的看门狗式配合。

这是嵌入式量产 OTA 的核心安全网。**强烈建议加上**。

---

## 3. 必须修改的设计点（汇总，按优先级）

| # | 修改 | 影响层 | 优先级 |
|---|---|---|---|
| M1 | Bus 从 Core 移出，独立 `ENABLE_BUS`，由用到 Bus 的模块自动开 | Core | P0 |
| M2 | profile 加 `PROFILE_NET_RUNTIME`（或改用 bitmask） | profile | P0 |
| M3 | Web 启动条件改为 `WiFi.connected ‖ WiFi.config_portal`，并在 §3.2 写清 | Web | P0 |
| M4 | 增加 §7.8 "Arduino Core 2.x / 3.x 兼容矩阵"，明确支持版本与 `ESP_ARDUINO_VERSION_MAJOR` 条件编译 | 全局 | P0 |
| M5 | OTA + Watchdog 协议具体化：摘任务/yield/重新加入/brownout 处理 | Update | P0 |
| M6 | Bus 多任务语义：固定为"loop 任务内 publish"约束，模块内部入队 | Core | P0 |
| M7 | Config 强制 namespace/key 长度校验（≤15）+ 编译期 static_assert | Core | P1 |
| M8 | LittleFS 首次挂载策略写进 §7.5（挂载失败但不 halt） | Runtime | P1 |
| M9 | ESP32-C3 差异化默认值（缓冲、wakeup 源） | 全局 | P1 |
| M10 | OTA 双分区回滚：`markCurrentValid()` + 可选回滚看门狗 | Update | P1 |
| M11 | OTA 路由强制要求 setAuth 改过默认密码，否则不启动 | Web/Update | P1 |
| M12 | Captive Portal DNS 拦截（AP 模式启动 DNSServer） | Network | P2 |

---

## 4. 可选优化建议

- **O1**：把 `Esp32Base*` 这种长前缀替换为命名空间收敛：`namespace esp32base { class WiFi; class Web; ... }`。用户可以 `using namespace esp32base` 或写 `esp32base::WiFi::connect()`，比 `Esp32BaseWiFi::connect()` 干净。如果一定要静态类风格，前缀建议缩成 `Eb32` 或 `E32`。
- **O2**：`Esp32BaseLog` 用 macro + 编译期 level 过滤，**保证 `LOG_D` 在 release 构建里完全消失**（连 format 字符串都不进 .rodata）。EspBase 已经这么做，新方案要继承。
- **O3**：`Esp32BaseSystem` 暴露 `restart(reason)` 接口，**所有重启走它**。它内部做 flushAll → 日志 → `esp_restart()`。强烈建议把 `ESP.restart()` 列入 lint 黑名单（CI 检查）。
- **O4**：`Esp32BaseBus` 事件名改用 **enum 或 const char\* 常量表**（每个模块自己定义自己的事件常量），不要让用户硬编码字符串。这样改名时编译期能查出来。
- **O5**：增加 `Esp32BaseHealth`（属于 Core 或 Runtime），周期性把 free heap、min heap、reset reason、uptime 推到 Bus 上，供其他模块订阅做自我保护。生产固件标配。
- **O6**：每个 profile 编译产物在 CI 里输出 `firmware.map` 摘要 + `text/data/bss` 数字，写到一张 markdown 表里发布。让用户看到 "PROFILE_CORE = 110 KB flash, 12 KB RAM" 这种实数。EspBase 没这么做，新方案做了会成为差异化卖点。
- **O7**：`Esp32BaseFs` 同时支持 LittleFS 和 SPIFFS（编译期二选一）。SPIFFS 已被官方 deprecate 但量产存量很大，提供一个迁移期开关无害。
- **O8**：`examples/` 每个示例都加 `platformio.ini` 的 `[env:esp32]` `[env:esp32s3]` `[env:esp32c3]` 三个 env，CI 矩阵编译。

---

## 5. 推荐的最终模块顺序

经过上述修订，**推荐的最终分层与依赖如下**：

```text
Layer 0 (Core, 必装)
    Esp32BaseLog        ← header-only macros + 1 source
    Esp32BaseConfig     ← NVS, deferred write, flushAll
    Esp32BaseSystem     ← heap/flash/reset/restart/uptime/markOtaValid

Layer 1 (Runtime, 可选)
    Esp32BaseBus        ← 同步事件总线（loop 任务 only）  ★ 从 Core 下沉
    Esp32BaseWatchdog   ← TWDT 封装
    Esp32BaseSleep      ← deep / light sleep
    Esp32BaseFs         ← LittleFS（默认不格式化）
    Esp32BaseHealth     ← 周期性诊断推送（optional）       ★ 新增

Layer 2 (Network)
    Esp32BaseWiFi       ← STA + AP + 状态机 + 配网门户
    Esp32BaseDns        ← Captive Portal DNS（AP 模式）   ★ 新增
    Esp32BaseNtp
    Esp32BaseMdns

Layer 3 (Web)
    Esp32BaseWeb        ← WebServer + Auth + 路由 + JSON helpers
    Esp32BaseWebStatus  ← /info, /chip, /firmware
    Esp32BaseWebWiFi    ← /wifi 配置页

Layer 4 (Update)
    Esp32BaseOta        ← 状态机 + 写入 + 双分区回滚
    Esp32BaseWebOta     ← /ota 上传页与 API

入口
    Esp32Base           ← 固件信息 + profile + begin/handle 调度
```

依赖方向严格自上而下；同层模块互不依赖。

**变更点 vs 原方案：**
1. Bus 从 Core 下沉到 Runtime（M1）。
2. 新增 `Esp32BaseDns`（Captive Portal）于 Network 层。
3. 新增 `Esp32BaseHealth`（O5）于 Runtime 层。
4. `Esp32BaseSystem` 承担统一 restart（O3）和 OTA 标记（M10）。

---

## 6. 推荐的实施阶段

不推荐"一次性大爆炸式重写完再合并"。即使允许破坏 API，仍建议按 **5 个阶段提 PR**，每阶段都让库可编译可烧录：

### Phase 1 —— Core 重置（约 1 周）
1. 删旧 `Esp32Base*.{h,cpp}`。
2. 新建 `src/core/` 三件套 + `src/Esp32Base.{h,cpp}` 骨架。
3. 实现 `ESP32BASE_PROFILE_CORE` 与 `ENABLE_*` 宏体系（先不带 Bus）。
4. CI：ESP32 / S3 / C3 三芯片只编 CORE profile。
5. `examples/core_basic` 跑通。

**验证**：CORE 静态 RAM、flash 占用基线。

### Phase 2 —— Runtime（约 1 周）
1. 加入 Bus、Watchdog、Sleep、Fs、Health。
2. `PROFILE_RUNTIME`、`PROFILE_NET_RUNTIME`（先占位，待 WiFi 加入）。
3. LittleFS 首次挂载策略验证。
4. `examples/runtime_basic` 跑通。

### Phase 3 —— Network（约 1.5 周）
1. WiFi 状态机重写（CONNECTING / CONNECTED / CONFIG_MODE / RETRY_BACKOFF）。
2. 非阻塞重连 + 分级 backoff（参考 EspBase 的 15s/60s/300s 三档）。
3. Captive Portal DNS 拦截。
4. NTP / mDNS 延迟启动。
5. WiFi 事件回调入队 → loop 任务 publish 模式定型。
6. `examples/wifi_basic` + `examples/net_runtime` 跑通。

**关键验证**：弱信号 / 错误密码 / AP 切回 STA 等真实场景。

### Phase 4 —— Web（约 1 周）
1. WebServer + Basic Auth + JSON helpers (`sendJson`/`writeJsonEscaped`)。
2. `WebStatus` / `WebWiFi` 拆分。
3. `examples/web_config` 跑通。

### Phase 5 —— Update（约 1 周）
1. OTA 状态机 + 双分区 markValid。
2. WebOTA 路由 + 强制 setAuth 检查。
3. Watchdog 摘任务 / brownout 处理。
4. `examples/web_ota` + `examples/full_diagnostics` 跑通。

**关键验证**：OTA 中途断电、坏固件回滚、低压 brownout。

### Phase 6 —— 收尾（约半周）
- 文档全部按新结构重写。
- 资源占用表写进 README。
- 三芯片 × 6 profile 的 CI 矩阵全绿。

**总工期约 5~6 周**，单人全职可控。

---

## 7. 必须先做原型验证的关键场景

在 Phase 1 之前，**先用一周做这 4 个原型**，因为它们决定整个架构能否成立：

1. **Bus 的多任务语义原型**：用 WiFi.onEvent 在 LWIP 任务里触发 publish，验证 (a) 入队 + loop publish 的延迟与丢失率，(b) 真锁版本的开销。决定 Bus 的最终契约（M6）。
2. **OTA + Watchdog + brownout 原型**：跑一个 4MB 固件 OTA，故意在 30%/70% 断电，观察 TWDT、IWDT、brownout 的行为，决定 §7.7 的具体协议（M5）。
3. **Arduino Core 2.x ↔ 3.x 编译原型**：同一份代码，分别在 Arduino Core 2.0.14 和 3.0.4 上编译三芯片，列出所有 break。决定 §7.8 兼容矩阵（M4）。
4. **Profile 资源裁剪验证**：写 6 个空 main.cpp，分别只 `#define` 一种 profile，跑出 firmware.map，验证：CORE 真的没链接进 WiFi/WebServer/Update/LittleFS 任何符号。这是整个 profile 设计能否站住的命门。

---

## 8. 可能"看似合理但实际增加维护成本"的点

- **(警告) 13 个 `Esp32Base*` 公开类**：用户 `#include` 列表会很长。考虑提供一个 `Esp32BaseAll.h` 聚合头，按 `ESP32BASE_ENABLE_*` 条件 include 子模块，方便用户少写 `#include`。
- **(警告) Profile 与底层宏的优先级冲突**：如果用户同时 `#define ESP32BASE_PROFILE_CORE` 和 `#define ESP32BASE_ENABLE_WIFI`，谁赢？方案没说。**必须明确：profile 先展开，用户的 ENABLE_* 后覆盖**。否则会成为长期支持问题。
- **(警告) 事件总线名靠字符串**：`"wifi.connected"` 拼错不会编译报错。建议每个模块导出 `static constexpr const char* EVENT_CONNECTED = "wifi.connected"` 类常量，让用户写 `bus.subscribe(WiFi::EVENT_CONNECTED, ...)`。
- **(警告) Esp32BaseSystem 职责膨胀**：`heap / flash / reset reason / restart` 已经混了。如果再塞 OTA 标记（M10）、统一 restart（O3），就变成第二个上帝类。考虑改名 `Esp32BaseLifecycle` 或拆出 `Esp32BaseLifecycle`（管 restart/sleep/flushAll）+ `Esp32BaseDiag`（管 heap/flash/reset reason）。
- **(警告) 五层划分本身**：Layer 名称 "Update" 里只有 OTA，过细。可以并入 Web 层叫 "Web & Update"，或者让 OTA 成为 Web 的一个子模块（命名 `Esp32BaseWebOta` 暗示了这个关系）。**当前划分已是边界**，再拆就过度设计。

---

## 9. 替代架构提案（如果允许更激进）

当前方案是"显式 profile + 静态类 + 编译期裁剪"，已经相当优秀。如果允许更现代的做法，提供一个**替代方案**供对照：

### 替代方案 A：基于 trait/policy 的轻量组合（**不推荐生产，列出供参考**）

```cpp
using Stack = esp32base::Stack<
    Core,
    Runtime<WatchdogOn, SleepOn, FsLittle>,
    Network<WiFiSta, NtpOn, MdnsOff>,
    Web<AuthBasic, RouteCap<16>>,
    Update<OtaWithRollback>
>;

void setup() { Stack::begin(); }
void loop()  { Stack::handle(); }
```

**优点**：编译期完全裁剪、无运行时分支、依赖关系靠模板参数表达。
**缺点**：模板错误信息恐怖、Arduino IDE 用户难懂、调试困难。
**结论**：**不适合**作为开源 Arduino 库的主线。当前的"宏 + 静态类"是更好的工程现实选择。

### 替代方案 B：保持当前方案，但用静态注册表去中心化（**推荐作为 v2.0 演进方向**）

```cpp
// 每个模块自己声明
ESP32BASE_REGISTER_MODULE(WiFi, /*priority*/ 30, /*depends_on*/ NONE);
ESP32BASE_REGISTER_MODULE(Ntp,  /*priority*/ 50, /*depends_on*/ WiFi);
ESP32BASE_REGISTER_MODULE(Web,  /*priority*/ 60, /*depends_on*/ WiFi);

// Esp32Base.cpp 不再硬编码顺序
void Esp32Base::handle() {
    for (auto* m : registry::sorted_by_priority()) {
        if (m->prerequisites_satisfied()) m->handle();
    }
}
```

**优点**：新增模块零修改 `Esp32Base.cpp`；依赖关系显式可读；可输出"启动顺序图"用于调试。
**缺点**：static init order fiasco 风险（解决方案：用函数静态局部变量 + Meyer's singleton）；编译期裁剪需要每个模块的 .cpp 整体被宏包裹（这正好和方案 §4.6 一致）。
**结论**：**v1.0 用方案的硬编码顺序（简单可控），v2.0 演进到注册表（解决扩展痛点）**。这是我作为长期维护者的具体建议。

---

## 10. 实机验证的关键场景清单（在原方案 §9.2 基础上扩充）

打 ✅ 的是原方案已列，打 ➕ 的是必须新增：

- ✅ CORE：无 AP、无 Web、无 FS 挂载。
- ✅ RUNTIME：Watchdog、Sleep、LittleFS 文件读写。
- ✅ NET：无凭证进 AP 配网，有凭证 STA 非阻塞连接，NTP / mDNS 延迟启动。
- ✅ WEB：Basic Auth、状态 API、WiFi 配置 API、自定义路由。
- ✅ FULL：OTA 成功、未授权 OTA 被拒、OTA 期间 watchdog 安全。
- ✅ Config：deferred 多条写入在 restart / deep sleep 前全部落盘。
- ✅ JSON 转义：SSID / hostname 含引号、反斜杠、控制字符仍合法。
- ➕ **OTA 中途断电**：30% / 70% / 99% 三个点位强制断电，确认下次启动回滚到旧分区且能正常工作。
- ➕ **OTA 后未 markValid**：刷新固件但不调用 markValid，确认下次启动自动回滚。
- ➕ **WiFi 路由器掉电**：STA 模式下路由器断电 5 分钟再恢复，确认重连成功且 WebServer 自动可访问。
- ➕ **Captive Portal**：手机连 AP 不输入 URL，确认弹出配网页面（DNS 拦截生效）。
- ➕ **NVS 写满**：故意写到 NVS 满，确认 setStr 返回 false 而不是死循环或崩溃。
- ➕ **LittleFS 首次挂载**：全新设备 + 不开 auto-format，确认日志清晰、其他功能不被 halt。
- ➕ **Brownout during OTA**：用可调电源把 VCC 降到 2.7V 触发 brownout，确认设备不变砖。
- ➕ **C3 单核重负载**：WiFi + Web + LittleFS 同时跑，观察 loop 周期，确保 watchdog 不被饿死。
- ➕ **Arduino Core 2.x 与 3.x 双跑**：两个版本各跑一遍 FULL profile，对比启动日志与功能。
- ➕ **超长 SSID / 超长密码**：SSID 32 字符、密码 63 字符的边界值。
- ➕ **deferred write + restart 竞争**：在 deferred write 队列非空时调用 restart()，确认 flushAll 真的写完才重启。
- ➕ **多模块 publish 风暴**：1Hz 批量推送 100 个事件，观察 Bus 是否丢失/阻塞 loop。

---

## 11. 一句话总结

> **方案的方向、分层、profile 思路都是对的，比当前实现明显更可维护。**
> **但必须修订 12 处（M1~M12）才能进入实施，其中 Bus 下沉、profile 加 NET_RUNTIME、Arduino Core 兼容矩阵、OTA 回滚是 P0。**
> **建议按 5 阶段 PR 推进，先做 4 个关键原型验证，约 6 周可以交付一个长期可维护的 v1.0。**
