# Esp32Base 项目评估问题清单（第二轮）

> 评估日期：2026-05-03
> 评估范围：src/ 源码 + docs/ 文档
> 原则：只读评估，未做任何修改
> 备注：基于用户第一轮修改后的代码重新评估

---

## 已修复的问题

| # | 原问题 | 修复方式 |
|---|--------|----------|
| 1.1 | begin() 缺少 Sleep 模块初始化 | `Esp32Base.cpp:55-57` 增加了 `#if ESP32BASE_ENABLE_SLEEP` 分支 |
| 2.1 | mbedtls SHA256 API 在 Core 3.x 不兼容 | `Esp32BaseOta.inc:79-85` 新增 `sha256Starts()` 做版本判断 |
| 3.3 | DNS 启动条件重复检查 | `Esp32Base.cpp:83` 去掉了冗余的 WiFi state 检查 |
| 3.4 | copySafe 函数重复定义 | 新增 `core/Esp32BaseUtil.h`，统一到 `esp32base_internal::copySafe` |
| 3.5 | setStr 字符串长度边界值 | `Esp32BaseConfig.cpp:124` 上限从 4000 改为 3999 |
| 4.2 | wasWatchdogReset() 未实现 | `Esp32BaseWatchdog.inc:76-79` 通过 `esp_reset_reason()` 实现 |
| 6.1 | OTA handle() 为空函数 | `Esp32BaseOta.inc:96-109` 实现了 mark-valid timeout 自动回滚机制 |
| 13 | Esp32BaseAliases.h 别名层不必要 | 文件已删除 |

---

## 剩余问题

### 一、架构文档与代码不一致

#### 1.2 Web 内部模块不存在（未修复）

**文档** `docs/01_architecture.md:100-104` 声明 Web 层包含内部模块：
- `Esp32BaseWebStatus`
- `Esp32BaseWebWiFi`
- `Esp32BaseWebUtil`

**代码**：`src/web/` 下只有 `Esp32BaseWeb.h` 和 `Esp32BaseWeb.inc`，上述内部模块文件不存在。

**影响**：文档描述与实际架构不符，开发者可能按文档寻找不存在的模块。

---

### 二、未实现功能

#### 4.1 resetReason() 和 wakeReason() 硬编码（未修复）

**位置**：`src/core/Esp32BaseSystem.cpp:48-54`

```cpp
const char* Esp32BaseSystem::resetReason() {
    return "unknown";
}

const char* Esp32BaseSystem::wakeReason() {
    return "unknown";
}
```

**问题**：声明了功能但未实现。ESP-IDF 提供了 `esp_reset_reason()` 和 `esp_sleep_get_wakeup_cause()` 可以获取真实原因。Watchdog 模块已经用到了 `esp_reset_reason()`，System 模块完全可以复用。

---

### 三、代码质量问题

#### 3.2 String 频繁分配可能导致 heap 碎片（未修复）

**位置**：`src/web/Esp32BaseWeb.inc:64-88, 90-106, 148-174` 等

`escapeJson()` 和 `escapeHtml()` 返回 `String` 对象，`handleStatus()`、`handleChip()`、`handleFirmware()` 等函数中大量使用 `String` 拼接 JSON。

**改进**：Web 层已引入 `beginChunked()` / `sendChunk()` / `sendEscapedHtmlChunk()` 用于 HTML 页面的流式输出（`handleWifiPage`、`handleOtaPage`），但 JSON API 响应仍然使用 `String` 拼接。

**影响**：中低优先级，但长时间运行的设备上 JSON API 频繁调用可能加剧 heap 碎片化。

---

### 四、编译模型

#### 5.1 .inc include 模式（本质未变，扩展名改善语义）

**位置**：`src/Esp32Base.cpp:199-231`

```cpp
#if ESP32BASE_ENABLE_BUS
#include "runtime/Esp32BaseBus.inc"
#endif
```

**变化**：从 `.cpp` 改为 `.inc`，语义上更清晰（这些是 include 文件，不是独立编译单元）。`library.json` 的 `srcFilter` 也相应更新为只编译 `*.cpp` 和 `core/*.cpp`。

**仍然存在的问题**：
1. 非标准 C++ 编译模型，IDE 导航和静态分析工具可能无法正确解析
2. 如果用户自定义 `src_filter` 或手动编译，可能导致符号缺失
3. 如果某处意外将 `.inc` 文件加入编译，会产生多重定义错误

**评价**：`.inc` 扩展名比 `.cpp` 更能传达意图，这是一个改善。但编译模型的本质风险仍然存在。

---

### 五、新观察

#### 5.2 Esp32BaseAll.h 无条件引入所有头文件

**位置**：`src/Esp32BaseAll.h`

```cpp
#include "runtime/Esp32BaseBus.h"
#include "runtime/Esp32BaseWatchdog.h"
// ... 所有模块头文件，无 #if ESP32BASE_ENABLE_* 保护
```

**问题**：库的核心设计是"按需裁剪"，但 `Esp32BaseAll.h` 无条件 include 所有头文件声明。虽然实现（.inc）仍受 profile 控制不会链接，但：
1. 编译器仍需解析所有头文件声明，增加编译时间
2. 如果某个头文件引用了未启用模块的依赖（当前没有，但未来可能），可能编译失败

**建议**：每个 include 加 `#if ESP32BASE_ENABLE_*` 保护，与 `Esp32Base.h` 的做法保持一致。

---

#### 5.3 Esp32BaseConfig deferred string 改为动态分配

**位置**：`src/core/Esp32BaseConfig.cpp:22`

```cpp
struct PendingItem {
    // ...
    char* strValue;  // 之前是 char strValue[128];
    // ...
};
```

**变化**：`strValue` 从固定 128 字节数组改为 `char*` 指针，`setStrDeferred` 中使用 `malloc` 分配，`flushIndex` 和 `clearPendingString` 中使用 `free` 释放。

**好处**：不再限制 deferred string 为 128 字节，可以存更长的字符串（受 `setStr` 的 3999 字节上限约束）。

**新风险**：
1. `malloc` 在 ESP32 上可能失败，`setStrDeferred` 已检查 `if (!copy) return false;` ✅
2. 如果 `flushIndex` 在写入成功后忘记调用 `clearPendingString`，会产生内存泄漏。当前代码 `flushIndex` 中正确调用了 ✅
3. `allocPending` 复用已有 slot 时，`setIntDeferred` / `setBoolDeferred` 都调用了 `clearPendingString` ✅

**评价**：实现正确，但增加了动态内存管理的复杂度。对于嵌入式系统，动态分配需要格外小心。

---

#### 5.4 OTA mark-valid timeout 机制

**位置**：`src/update/Esp32BaseOta.inc:96-109`

```cpp
void Esp32BaseOta::handle() {
#if ESP32BASE_OTA_REQUIRE_MARK_VALID
    if (millis() - g_otaBootMs >= ESP32BASE_OTA_MARK_VALID_TIMEOUT_MS) {
        // 检查分区状态，如果仍是 PENDING_VERIFY 则自动回滚
    }
#endif
}
```

**评价**：这是一个很好的可靠性改进。通过 `ESP32BASE_OTA_REQUIRE_MARK_VALID` 宏控制，默认关闭（`#define ESP32BASE_OTA_REQUIRE_MARK_VALID 0`），用户可以按需开启。超时时间默认 30 秒。

---

#### 5.5 WiFi 凭证同步写入的设计决策

**位置**：`src/network/Esp32BaseWiFi.inc:103-106`

```cpp
if (persist) {
    // Explicit credential saves stay synchronous so a reboot right after the form submit keeps the new WiFi config.
    Esp32BaseConfig::setStr("eb_wifi", "ssid", g_wifiSsid);
    Esp32BaseConfig::setStr("eb_wifi", "pass", g_wifiPassword);
}
```

**评价**：加了注释说明设计理由——防止用户提交表单后立即重启导致新凭证丢失。这是一个合理的权衡，不再是问题。

---

## 问题汇总

| # | 严重度 | 类别 | 问题 | 状态 |
|---|--------|------|------|------|
| 1 | **高** | 架构一致性 | begin() 缺少 Sleep 模块初始化 | ✅ 已修复 |
| 2 | **高** | 兼容性 | mbedtls SHA256 API 在 Core 3.x 可能不兼容 | ✅ 已修复 |
| 3 | 中 | 性能 | 凭证保存使用同步 NVS 写入 | ✅ 已修复（加了注释说明理由） |
| 4 | 中 | 性能 | Web 层 String 频繁分配可能碎片化 heap | 未修复 |
| 5 | 低 | 代码质量 | DNS 启动条件重复检查 | ✅ 已修复 |
| 6 | 低 | 代码质量 | copySafe 函数重复定义 | ✅ 已修复 |
| 7 | 低 | 正确性 | setStr 字符串长度边界值 | ✅ 已修复 |
| 8 | 中 | 功能缺失 | resetReason() / wakeReason() 未实现 | 未修复 |
| 9 | 中 | 功能缺失 | wasWatchdogReset() 未实现 | ✅ 已修复 |
| 10 | 中 | 编译模型 | .cpp/.inc include 模式非标准 | 本质未变（.inc 语义改善） |
| 11 | 低 | 文档一致性 | Web 内部模块文档与代码不符 | 未修复 |
| 12 | 低 | API 设计 | OTA handle() 为空函数 | ✅ 已修复 |
| 13 | 低 | 过度设计 | Esp32BaseAliases.h 别名层不必要 | ✅ 已修复 |
| 14 | 低 | 裁剪一致性 | Esp32BaseAll.h 无条件引入所有头文件 | 新发现 |

**当前剩余未修复问题：4 个（1 个中 + 3 个低）**
