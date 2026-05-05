# Esp32Base 架构重构方案 V2 —— 专业评估报告

**评审视角**：资深嵌入式系统架构师 / ESP32 Arduino Core 库维护者 / 长期量产设备固件负责人
**评审对象**：`ESP32BASE_ARCHITECTURE_REDESIGN_PLAN_V2.md`
**对照基线**：`ESP32BASE_ARCHITECTURE_REDESIGN_REVIEW.md`（V1 评审）
**评审日期**：2026-05-03

---

## 0. 总体结论

**结论：V2 已经达到可以进入实施阶段的成熟度，强烈推荐采用。**

> V1 评审的 **12 项必改点 100% 命中并落实**，部分点（OTA 回滚、Captive DNS、Arduino Core 兼容矩阵）已经写得比 V1 评审建议更细。
> **仍存在 9 处需要修订的问题**（多为实现细节或新引入的盲点），但全部可在 Phase 0 / Phase 1 之内补齐，**不构成架构层面的阻塞性风险**。

| 维度 | V1 评分 | V2 评分 | 变化 |
|---|---|---|---|
| 架构合理性 | 8/10 | **9/10** | Bus 下沉、Health 落地、Captive DNS 加入 |
| 资源裁剪能力 | 7/10 | **9/10** | NET_RUNTIME 补齐、profile 与 ENABLE_* 双层规则明确 |
| ESP32 专用化收益 | 9/10 | **9/10** | 三芯片差异化默认值已明确 |
| API 与长期维护 | 7/10 | **8/10** | Esp32BaseAll.h 聚合头加入；命名空间 V2 演进路径已声明 |
| 可靠性与安全 | 6/10 | **8/10** | OTA 回滚、Watchdog 联动、默认密码拒绝、JSON escape 全面闭环 |
| 实施风险 | 8/10 | **9/10** | Phase 0 原型先行，分阶段交付，单人 6~7 周可控 |

**一句话判断**：V2 是一个工程成熟度很高的方案，可以以此为蓝本进入 Phase 0 原型阶段。剩余问题不影响整体架构走向。

---

## 1. V1 12 个必改点的逐项核对

| # | V1 问题 | V2 处理 | 落地位置 | 评价 |
|---|---|---|---|---|
| M1 | Bus 不默认进 Core | ✅ Bus 已下沉到 Runtime 层；§2.1 明确 "Core 不包含 Event Bus" | §2.1, §2.2, §3.1 | **完全采纳**，且 §2.1 写明设计理由 |
| M2 | 增加 NET_RUNTIME profile | ✅ 已加 `ESP32BASE_PROFILE_NET_RUNTIME` | §3.4, §21 | **完全采纳**，且 §3.4 写明理由 |
| M3 | Web 在 connected/config_portal 均启动 | ✅ §2.4 显式约束；§6.2 步 4 写入 handle 调度 | §2.4, §6.2 | **完全采纳**，措辞清晰 |
| M4 | Arduino Core 2.x/3.x 兼容矩阵 | ✅ §13 整章覆盖；含 `ESP_ARDUINO_VERSION_MAJOR` 条件编译 | §13.1, §13.2 | **完全采纳**，比 V1 评审建议还细 |
| M5 | OTA + Watchdog 协议 | ✅ §12.3 四步协议（remove/yield/restore/失败路径恢复） | §9.1 API, §12.3 | **完全采纳**，API 已落到 Watchdog 类 |
| M6 | Bus loop-task-only 发布语义 | ✅ §7.2 明确"WiFi callback 只入队，loop publish" | §7.2, §10.1 | **完全采纳** |
| M7 | NVS namespace/key 长度限制 | ✅ §8.2 "namespace 1..15, key 1..15"；非法返回 false | §8.2 | **完全采纳**；建议补编译期 macro 检查（见 P1） |
| M8 | LittleFS 首次挂载策略 | ✅ §9.3 五步策略，挂载失败不 halt | §9.3 | **完全采纳** |
| M9 | ESP32-C3 差异 | ✅ §13.2 + §9.2 (Sleep) + §11.2 (Web 容量) 多处覆盖 | §9.2, §11.2, §13.2 | **完全采纳** |
| M10 | OTA 回滚 | ✅ §12.5 提供 markCurrentValid / isRollbackPossible / rollbackAndRestart；含 timeout 自动回滚的可选机制 | §8.3 (System 底层), §12.5 | **完全采纳**，比 V1 评审建议更细 |
| M11 | OTA 默认密码拒绝 | ✅ §11.3 三条检查（auth enabled / 已显式设置 / 不是默认值） | §11.3 | **完全采纳**，但默认值检测机制需补实现说明（见 P3） |
| M12 | Captive Portal DNS | ✅ 新增 `Esp32BaseDns` 模块；§10.2 启停规则明确 | §2.3, §10.2 | **完全采纳** |

**结论：V1 12 项 100% 命中，无遗漏、无打折。** 这种采纳率在我十多年看到的方案修订里属于上等。

---

## 2. V2 新引入或仍存在的问题

V1 → V2 改进过程中引入了一些新的实现细节，以及若干 V1 没注意到的新盲点。按优先级列出：

### P1 (必改) —— Profile 表与文本描述对 Bus 的处理不一致

§3.3 `PROFILE_NET` 启用列表里**没有** Bus，但 §21 表里 NET 的 Bus 是 `optional`。两处需要对齐：

- 推荐做法：profile 展开为**默认值**，用户可用 `ESP32BASE_ENABLE_BUS=1` 覆盖。
- §3 文本应明确："NET profile 默认不启用 Bus；如果业务需要事件订阅，请显式启用"。
- §21 表里把 `optional` 改成 `no（可显式开启）`，与其他列保持同一种表述。

类似问题：§3.5 `PROFILE_WEB` 启用列表里写了 `Bus`，但 Web 模块本身并不强依赖 Bus（它直接调用 `WiFi::isConnected()`）。需要明确：**Bus 是 Web profile 的"建议"还是"强制"**？建议改为"建议但可关"，并在依赖检查里允许关闭。

### P2 (必改) —— Profile 与 ENABLE_\* 的展开顺序未规定，存在隐患

§4 写 "Profile 先展开。用户显式设置的 ENABLE_\* 可覆盖 profile 默认值。"

工程上要做到这一点，**`Esp32BaseProfile.h` 必须用 `#ifndef` 包裹每条 profile 默认**，否则 `-DESP32BASE_ENABLE_WIFI=0` 会触发 "redefine without undef" 警告甚至报错。建议在 §4 增加实现要求：

```cpp
// Esp32BaseProfile.h
#if defined(ESP32BASE_PROFILE_NET)
  #ifndef ESP32BASE_ENABLE_WIFI
    #define ESP32BASE_ENABLE_WIFI 1
  #endif
  // ...其余模块同理
#endif

// 依赖检查必须在所有展开之后执行
#if ESP32BASE_ENABLE_WEB && !ESP32BASE_ENABLE_WIFI
  #error "ESP32BASE_ENABLE_WEB requires ESP32BASE_ENABLE_WIFI"
#endif
```

否则量产用户用 `-D` 覆盖时会踩坑。**这是 Phase 1 必须先固化的契约**。

### P3 (必改) —— OTA "默认密码拒绝" 的实现细节不明确

§11.3 写"密码不是默认值"，但 V2 没说：
- 默认值是什么？（`admin/admin123`？ 来自 `EB_WEB_AUTH_USER/PASS` 宏？）
- 检测方式是字符串比较还是某种 flag？
- 用户改了用户名但密码仍是默认怎么办？

**推荐做法**（更鲁棒）：

```cpp
// 不依赖"密码字面值检测"，改用"应用是否调用了 setAuth"
class Esp32BaseWeb {
  static bool _authSetByApplication;  // 默认 false
  static void setAuth(const char* user, const char* pass) {
    _authSetByApplication = true;
    // ...
  }
};

class Esp32BaseWebOta {
  static bool begin() {
    if (!Esp32BaseWeb::isAuthEnabled() ||
        !Esp32BaseWeb::isAuthSetByApplication()) {
      ESP32BASE_LOG_E("OTA", "refuse to start: auth must be set by setAuth()");
      return false;
    }
    // ...
  }
};
```

这样既不需要硬编码默认值字符串，也避免"用户用了 admin/admin124 也算修改了"这种漏洞。**§11.3 应改成这种语义**。

### P4 (必改) —— OTA 期间 NVS deferred write 应暂停

V2 把 `Esp32BaseConfig::flushAll()` 接入 restart/sleep 路径很好，但漏了一个场景：**OTA 写入和 NVS deferred flush 都在写 flash**，并发会引起：
- 写入抢占 → 单条 NVS 写延迟变长 → loop 卡顿 → WebServer 超时 → 上传失败。
- 极端情况下，两个都在 esp_partition 临界区里 spin。

**推荐补充 §12.2**：

> OTA 进入 UPLOADING 状态后，`Esp32BaseConfig::handle()` 暂停 deferred flush。OTA 进入 SUCCESS / FAILED / IDLE 后恢复。

这一项不加，OTA 在高 NVS 写入场景下会偶发失败，且**很难复现和定位**。

### P5 (必改) —— OTA 缺少 SHA256 校验

V2 §12.1 状态机有 `VERIFYING` 但全文没提**固件摘要校验**。EspBase 老库的 `EbOTA::startLocalUpdate(size, expected_digest)` 已经支持 SHA256。

**生产 OTA 没有 hash 校验是不可接受的**，原因：
- 网络途中字节翻转（即使 TCP 校验通过，应用层错误也常见）。
- 上传方上传错文件。
- 某些代理/路径插入广告或 BOM 头。

**必须补充 §12.2**：
```cpp
bool startUpload(size_t totalSize, const char* expectedSha256Hex = nullptr);
// 内部：Update.setMD5(...) 或自行 sha256 流计算
// 写入完成后比较，匹配才进 SUCCESS
```

`expectedSha256Hex` 可由 Web OTA 表单的 `X-Sha256` HTTP header 传入，应用按需启用。第一版至少要有"可选"的 SHA256，不必强制。

### P6 (强烈建议) —— 缺少 PROFILE_WEB_RUNTIME

V2 §3.5 `PROFILE_WEB = NET + Web + Bus`（不含 Watchdog/Sleep/Fs）。

但实际"小型 Web 配置型量产设备"的常见组合是 **WiFi + Web + Watchdog + Fs（无 OTA）**——例如一个传感器节点，固件靠烧录器升级，但需要看门狗保活和文件存储。

V2 §21 表里这个组合用户必须写：
```cpp
#define ESP32BASE_PROFILE_WEB
#define ESP32BASE_ENABLE_WATCHDOG 1
#define ESP32BASE_ENABLE_FS       1
```

可接受，但不优雅。**建议增加第 7 个 profile**：

```text
PROFILE_WEB_RUNTIME = WEB + Watchdog + Sleep + Fs + Health
```

7 个 profile 仍然管理得动；少 1 个会让常见组合写起来啰嗦。

### P7 (建议) —— Phase 0 原型应增加 "Captive Portal" 验证

V2 §16.1 列了 4 个原型，缺了一个 V1 评审里强调过的高风险项：**Captive Portal 在不同手机系统的检测兼容性**。

苹果系统访问 `captive.apple.com/library/test/success.html`，安卓访问 `connectivitycheck.gstatic.com/generate_204` 或 `clients3.google.com/generate_204`。如果 DNS 拦截 + Web 路由策略不对，**会出现"AP 连上但弹不出配网页"**——量产设备最容易被用户投诉的问题。

**Phase 0 必须加**：
- 用 iPhone / Android（最好两代以上）验证 captive portal 弹出。
- 拦截策略需返回 302/200 + 特定 HTML 触发系统弹窗。

### P8 (建议) —— Health 模块的采样周期 / Bus 容量需要默认值

V2 §9.4 Health 没说采样周期；§7 Bus 没给订阅表默认容量。实施时这些会成为讨论点。**推荐默认**：

```cpp
#ifndef ESP32BASE_HEALTH_TICK_INTERVAL_MS
  #define ESP32BASE_HEALTH_TICK_INTERVAL_MS  30000U  // 30s
#endif

#ifndef ESP32BASE_BUS_MAX_SUBSCRIBERS
  #if defined(CONFIG_IDF_TARGET_ESP32C3)
    #define ESP32BASE_BUS_MAX_SUBSCRIBERS  12
  #else
    #define ESP32BASE_BUS_MAX_SUBSCRIBERS  16
  #endif
#endif
```

写到方案里，避免 Phase 1/Phase 2 实施时来回讨论。

### P9 (建议) —— `Esp32Base::begin()` 失败处理策略未定义

V2 §6.1 列了 begin 顺序，但**没写"任何一步失败怎么办"**。EspBase 老库是任何模块失败就 halt。新方案应当区分：

- **Core 模块**（Log/Config/System）失败 → halt（已损坏到不可恢复）。
- **Runtime / Network / Web / Update 模块**失败 → log error，标记 fail，**继续启动其他模块**。例如 LittleFS 挂载失败时不应阻止 WiFi 启动。

V2 §9.3 已经把这条规则写进了 LittleFS（"挂载失败不 halt"），但没有把这条**全局规则**显式写出来。建议在 §6.1 末尾加一段"模块失败处理策略"。

---

## 3. 三视角评分

### 资深嵌入式系统架构师视角

- **优点**：分层方向单调；profile 思路清晰；状态机化启动。
- **关注**：P2（profile 展开顺序）、P9（失败策略）必须在 Phase 1 进入代码之前固化为契约。
- **结论**：架构层面**已经成熟**，可以进入实施。

### ESP32 Arduino Core 库维护者视角

- **优点**：Arduino Core 2.x/3.x 矩阵明确（§13.2）；C3 差异已写；不引入 AsyncWebServer 是正确取舍。
- **关注**：mDNS 在 IDF v5（Arduino Core 3.x）下 stop/start 复用 socket 偶有问题，需要 Phase 3 实测。
- **结论**：从库维护成本看，V2 比 V1 在版本兼容性上**质变**，可以长期维护。

### 长期量产设备固件负责人视角

- **优点**：OTA 回滚、Watchdog 联动、JSON escape、deferred flush、默认密码拒绝**全部闭环**。
- **关注**：P3（默认密码检测机制）、P4（OTA 期间 NVS pause）、P5（SHA256 校验）必须补，否则**量产 OTA 不敢用**。Captive Portal 兼容性（P7）必须 Phase 0 验证，否则用户体验断在第一步。
- **结论**：补完这三条后，**就是一个可以负责任地推到 1000 台设备的固件基础库**。

---

## 4. 必须修改的设计点（汇总）

| # | 修订 | 优先级 | 改动位置 |
|---|---|---|---|
| P1 | Bus 在 NET / WEB profile 的状态在 §3 与 §21 对齐；明确"建议但可关" | P0 | §3.3, §3.5, §21 |
| P2 | profile 展开必须 `#ifndef` 包裹；依赖检查放展开之后；写到 §4 | P0 | §4 |
| P3 | OTA 默认密码拒绝改用 "应用是否显式调用 setAuth" 的 flag 机制 | P0 | §11.3 |
| P4 | OTA UPLOADING 期间暂停 Config deferred flush | P0 | §12.2 / §8.2 |
| P5 | OTA 增加可选 SHA256 摘要校验 | P0 | §12.1, §12.2 |
| P6 | 增加 `PROFILE_WEB_RUNTIME` (WEB + Watchdog + Sleep + Fs + Health) | P1 | §3 新增 §3.7 + §21 |
| P7 | Phase 0 增加 Captive Portal 多设备弹出验证原型 | P1 | §16.1 |
| P8 | 给 Health 采样周期、Bus 订阅表容量、芯片差异化默认值 | P1 | §7, §9.4 |
| P9 | 明确 begin 失败策略：Core 失败 halt，其他失败继续 | P1 | §6.1 |

---

## 5. 可选优化建议

- **O1**：`Esp32BaseAll.h` 同时再提供 `Esp32BaseAlias.h`，给愿意短命名的用户：`using Wifi = Esp32BaseWiFi;` 等。零运行时开销，单纯减打字。
- **O2**：Phase 6 的 CI 矩阵应**前移到 Phase 1 完成时就开始建**，每个 phase 拓展矩阵；不要把测试基础设施留到最后。
- **O3**：`examples/web_ota` 与 `examples/full_diagnostics` 都用 PROFILE_FULL，重复。建议把 `full_diagnostics` 改为同时启用 Health + 自定义诊断面板，作为"如何观测设备健康"的样例。
- **O4**：`Esp32BaseSystem::restart()` 的 `reason` 参数应写入一个"重启日志环"（最近 N 次重启原因 + uptime + reset_reason），存 NVS。量产排障神器，成本 < 200 字节 NVS。
- **O5**：所有 `Esp32BaseXxx::logConfig()` 方法应统一到一个 `Esp32Base::logAllConfig()` 入口，避免用户自己一个一个调。
- **O6**：`Esp32BaseWatchdog::resetCount()` 明确文档：是当前 boot 计数还是跨 boot 计数？跨 boot 需要 NVS。建议提供两个 API：`currentBootResetCount()` 和 `lifetimeResetCount()`。
- **O7**：`Esp32BaseDns` 启停应捕获 UDP 53 端口占用错误并记日志；切换 STA → CONFIG_PORTAL → STA 时确保 stop/start 顺序正确。
- **O8**：library.json 的 `frameworks/platforms/dependencies` 字段也应按 profile 切：FULL profile 才声明 `LittleFS` 依赖，避免 CORE 用户被 LittleFS 元数据绑死。

---

## 6. 推荐的最终模块顺序（与 V2 基本一致，含微调）

```text
Layer 0  Core (永远启用)
    Esp32BaseLog        ← 编译期级别过滤
    Esp32BaseConfig     ← NVS + deferred + flushAll + name 长度校验
    Esp32BaseSystem     ← heap/flash/reset/uptime + 统一 restart + OTA 标记 + 重启日志环

Layer 1  Runtime (可选)
    Esp32BaseBus        ← 同步事件总线，loop-task-only publish
    Esp32BaseWatchdog   ← TWDT，含 remove/restore for OTA
    Esp32BaseSleep      ← deep / light，按芯片差异返回 bool
    Esp32BaseFs         ← LittleFS，挂载失败不 halt
    Esp32BaseHealth     ← 周期诊断，可选发布 health.tick

Layer 2  Network (可选)
    Esp32BaseWiFi       ← STA + AP 状态机 + 三档 backoff
    Esp32BaseDns        ← Captive Portal DNS 拦截
    Esp32BaseNtp        ← 延迟启动
    Esp32BaseMdns       ← 延迟启动 + WiFi 重连恢复

Layer 3  Web (可选)
    Esp32BaseWeb        ← WebServer + Auth + JSON helpers
    Esp32BaseWebStatus  ← 内置 status 路由（不公开 API）
    Esp32BaseWebWiFi    ← 内置配网路由（不公开 API）
    Esp32BaseWebUtil    ← 共用工具（不公开 API）

Layer 4  Update (可选)
    Esp32BaseOta        ← 状态机 + 写入 + SHA256 + 回滚 + Watchdog 联动 + NVS pause
    Esp32BaseWebOta     ← 内置 OTA 上传路由（不公开 API）

入口
    Esp32Base           ← profile 调度 + begin/handle + 启动诊断 + 重启日志
```

与 V2 §5.2 完全一致；新增 `Esp32BaseWebUtil`（V2 §14 目录里已有）。

---

## 7. 推荐的最终 profile 集合（在 V2 6 个基础上 +1）

| Profile | Core | Bus | Runtime(WD/Sleep/Fs/Health) | WiFi+DNS+NTP+mDNS | Web | OTA | 典型用途 |
|---|---|---|---|---|---|---|---|
| **CORE** | yes | no | no | no | no | no | 极简控制器 / 仅 NVS |
| **RUNTIME** | yes | yes | yes | no | no | no | 离线本地设备 |
| **NET** | yes | opt | no | yes | no | no | 纯联网（自带 MQTT） |
| **NET_RUNTIME** | yes | yes | yes | yes | no | no | 量产联网节点（推荐生产首选） |
| **WEB** | yes | yes | no | yes | yes | no | 局域网 Web 配置 |
| **WEB_RUNTIME** ★ | yes | yes | yes | yes | yes | no | Web 配置 + 看门狗 + Fs（无 OTA） |
| **FULL** | yes | yes | yes | yes | yes | yes | 完整管理 + Web OTA |

★ = 在 V2 6 个基础上**新增 1 个**（P6 建议）。共 7 个 profile，覆盖度从"够用"提升到"舒适"。

---

## 8. 实施阶段风险评估

| Phase | 主要风险 | 缓解 |
|---|---|---|
| Phase 0 原型 | **(高)** Captive Portal 在不同手机系统的兼容差异（P7 缺失） | 增加 P7 原型；至少覆盖 iPhone + Android |
| Phase 0 原型 | OTA + Watchdog 行为受 Arduino Core 版本影响 | 同时跑 2.x 和 3.x，结果写进设计文档 |
| Phase 1 Core | **(中)** profile 展开 + 依赖检查的宏写错（P2） | Phase 1 必须把 Profile.h 当合同写完，并加 1~2 个故意冲突的"反例编译 test" |
| Phase 2 Runtime | LittleFS 在 ESP32-C3 上行为差异（4MB flash 配 1MB FS） | 三芯片实测必须打 partition table 三套 |
| Phase 3 Network | mDNS 在 Arduino Core 3.x 下 stop/start 偶发失败 | 实测后决定是否需要 reset socket |
| Phase 3 Network | WiFi event 跨任务到 Bus 的延迟 / 丢失 | Phase 0 原型已测过；Phase 3 复测 |
| Phase 4 Web | WebServer 单线程，自定义路由耗时长会卡所有响应 | 文档明确：用户路由 < 200ms；提供性能诊断 |
| Phase 5 Update | **(高)** OTA 失败回滚链路（P5 SHA256 + P4 NVS pause + brownout） | Phase 5 必须用真断电 + 假固件做完整测试矩阵 |
| Phase 6 收尾 | CI 矩阵临时搭建质量差 | **建议把 CI 矩阵从 Phase 1 开始建**（O2） |

**总工期估计**：单人全职 6~7 周，每周 1 个 phase，phase 6 半周。

---

## 9. 必须实机验证的场景（在 V2 §16.3 基础上扩充）

打 ✅ 是 V2 已列；打 ➕ 是必须新增。

- ✅ CORE 三芯片：无 AP / 无 Web / 无 FS 挂载。
- ✅ RUNTIME：Watchdog feed、Sleep wake、LittleFS 读写。
- ✅ NET：AP 配网 → STA 切换。
- ✅ NET_RUNTIME：长时间运行。
- ✅ WEB：Auth / 状态 API / 配网 API / 自定义路由。
- ✅ FULL：OTA 成功 / 未授权拒绝 / Watchdog 安全。
- ✅ Captive Portal：手机连 AP 后 DNS 拦截。
- ✅ OTA 中途断电：30 / 70 / 99%。
- ✅ OTA 后未 markValid → 自动回滚。
- ✅ 路由器掉电 5 分钟恢复。
- ✅ NVS 写满。
- ✅ LittleFS 首次挂载（不开 auto-format）。
- ✅ JSON escape 边界字符。
- ✅ ESP32-C3 单核重负载 Watchdog 不误触。
- ✅ deferred write + restart 全部落盘。
- ➕ **OTA SHA256 校验**：上传被改 1 个字节的固件，必须拒绝并不刷写（P5）。
- ➕ **OTA 期间高频 NVS 写入**：业务在 1Hz 写 NVS 时启动 OTA，验证 NVS pause 生效（P4）。
- ➕ **OTA + brownout**：可调电源把 VCC 降到 2.7V，验证不变砖（V1 已建议，V2 仍应实测）。
- ➕ **Captive Portal 多设备弹出**：iPhone（最近 2 代 iOS）+ Android（最近 2 代）+ MacOS + Windows 各试一次（P7）。
- ➕ **OTA 默认密码拒绝**：不调 `setAuth()` 直接启动 OTA，必须拒绝注册路由（P3）。
- ➕ **WiFi 弱信号 + 再连接**：把 ESP32 移到 RSSI 接近门限的位置，做 1 小时连接稳定性 + min heap 监测。
- ➕ **mDNS 重连恢复**：多次断网/重连，验证 mDNS 服务可访问性。
- ➕ **跨 Arduino Core 版本回归**：每个 Phase 完成后，用 2.0.14 + 3.0.4 各跑一遍同一示例。

---

## 10. 替代架构？—— 不推荐推翻，但可在 V2.x 演进

**不需要替代架构**。V2 已经是当前阶段的最优解。

V1 评审时我提过两个替代方向：
1. **trait/policy 模板组合**（Stack<Core, Runtime<...>, Network<...>>）——模板错误难懂，不适合 Arduino 库。**仍不推荐**。
2. **静态注册表去中心化**——V2 §6.2 已经把它列为"未来演进方向"，决策与我推荐一致。**第一版保持硬编码顺序，v2.0 演进到注册表**。

唯一值得在 V2.x 时考虑的演进，是 **轻量命名空间收敛**：

```cpp
// 在第二版引入，向后兼容
namespace esp32base {
    using WiFi = Esp32BaseWiFi;
    using Web  = Esp32BaseWeb;
    using Ota  = Esp32BaseOta;
    // ...
}

// 用户新代码：
esp32base::WiFi::connect(ssid, pass);
// 旧代码继续可用：
Esp32BaseWiFi::connect(ssid, pass);
```

实施成本极低，但能把用户代码长度砍 30~40%。**作为 V1.1 即可加入**，不必等 V2.0。

---

## 11. 一句话总结

> **V2 已经把 V1 评审 12 个必改点全部消化，方案成熟度上了一个台阶，强烈推荐采用。**
> **Phase 0 原型阶段必须补上 Captive Portal 多设备验证；进入 Phase 1 之前必须固化 P1~P5 这 5 个细节（profile 展开、默认密码 flag、OTA pause NVS、SHA256、profile 表对齐）；其余 P6~P9 可在对应 phase 内修订。**
> **按 7 周节奏推进，单人全职可交付一个可量产的 v1.0。**
