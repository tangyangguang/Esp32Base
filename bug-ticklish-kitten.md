# Esp32Base 全仓只读评估报告

## Context

用户要求对仓库代码与文档做一次全面只读检查，**只输出问题和潜在 bug，不做任何修改**。本报告由 3 个并行 Explore 代理分别审计「核心 / 运行时」「网络 / Web / OTA」「文档 / 示例 / 脚本 / 构建」三块后，作者再回读关键代码核实，过滤了若干误报（已在文末单独列出）。

严重程度分级：
- **高**：直接破坏安全模型、可远程触发、或可能导致设备砖化 / 数据损坏
- **中**：在常见使用路径中可被触发，造成功能失败、信息泄露或可靠性下降
- **低**：风格、工程债、文档/打包细节、罕见路径

> 不附带修复方案。下面每一条都已通过文件路径 + 行号定位，便于核查。

---

## 1. 高危问题（HIGH）

### 1.1 Web 层多处明文打印用户名 / 密码 / Wi-Fi 凭据
- **位置**：[src/web/Esp32BaseWeb.inc:223](src/web/Esp32BaseWeb.inc:223), [:228](src/web/Esp32BaseWeb.inc:228), [:278](src/web/Esp32BaseWeb.inc:278), [:917](src/web/Esp32BaseWeb.inc:917), [:1517](src/web/Esp32BaseWeb.inc:1517), [:1609](src/web/Esp32BaseWeb.inc:1609), [:1657](src/web/Esp32BaseWeb.inc:1657), [:1665](src/web/Esp32BaseWeb.inc:1665)
- **现象**：`auth_request … password=%s`、`auth_loaded … password=%s`、`wifi form submitted ssid=%s password=%s`、`auth_saved_plain user=%s password=%s` 等日志将明文凭据写入串口 / 文件日志。
- **影响**：任何能取到日志（串口、`/esp32base/logs` Web 页面、轮转的 `/logs/eb_app.log`）的人即拿到管理员密码与 Wi-Fi 密码。开启 `ESP32BASE_ENABLE_FILELOG` 时，即使设备出厂后远程读取日志也能拿到。
- **已核实**：grep 命令直接命中多行，明文打印属实。

### 1.2 Web 全部 POST 接口无 CSRF / Origin / Referer 校验
- **位置**：`src/web/Esp32BaseWeb.inc` 全部表单与 API（Wi-Fi 配置、修改账号、OTA、reboot、清空日志等）。
- **现象**：`grep -n 'csrf\|CSRF\|SameSite\|Origin\|Referer' Esp32BaseWeb.inc` 无任何命中。仅有 HTTP Basic 认证 cookie/凭据，浏览器自动随附；攻击者用 `<form>` 或 `<img>` 自动触发请求即可改密码、发起 OTA、让设备重启。
- **影响**：管理员只要登过设备 Web 后端，访问外部恶意页面即可被远程改密 / 刷固件。
- **已核实**：grep 无命中。

### 1.3 webota 上传脚本超时设计与大固件不匹配
- **位置**：[scripts/esp32base_webota.py:330-333](scripts/esp32base_webota.py:330)
- **现象**：HTTP 连接级 `timeout`（默认 120 秒）覆盖整个 `_send_multipart` 多块发送过程。在弱网或较大固件（数 MB 以上）情境下，**整体上传**容易超过 120s，导致到一半连接被超时切断。设备侧已写入若干 chunk 后被中断，需要依赖 `Update.abort` 恢复。
- **影响**：现网升级失败率随固件大小线性升高，且失败语义不清（脚本看到的是 socket timeout，看不到设备侧 OTA 状态）。

> 备注：仅占用 `setStrDeferred` 中 `strcpy` 的代码（[src/core/Esp32BaseConfig.cpp:435](src/core/Esp32BaseConfig.cpp:435)）经核实**安全**——目标缓冲区按 `strlen(value)+1` 动态分配，长度上游已限制在 3999；不计入高危。

---

## 2. 中危问题（MEDIUM）

### 2.1 全局凭据 / 配置默认值是弱口令
- **位置**：[src/web/Esp32BaseWeb.inc:62-66](src/web/Esp32BaseWeb.inc:62)，默认 `g_authUser = "admin"`、`g_authPass = "admin"`，且没有「首次登录强制改密」机制。
- **影响**：用户若未主动调用 `setDefaultAuth()` 或 `saveAuth()`，所有出厂设备都用 `admin/admin` 暴露在局域网。

### 2.2 Wi-Fi 配置门户为开放 AP，SSID 可预测
- **位置**：[src/network/Esp32BaseWiFi.inc:214-228](src/network/Esp32BaseWiFi.inc:214)
- **现象**：`WiFi.softAP(g_configPortalSsid)` 不传密码，AP 名 `ESP32-Config-XXXX` 来自 MAC 后 16 bit。
- **影响**：任何人都能连入门户提交配置；MAC 推测降低物理识别门槛。

### 2.3 OTA 缺端到端签名校验，仅 SHA256（且为可选头）
- **位置**：[src/web/Esp32BaseWeb.inc:56](src/web/Esp32BaseWeb.inc:56)（接受 `X-Sha256` 头），[src/update/Esp32BaseOta.inc:332-377](src/update/Esp32BaseOta.inc:332)（`Update.begin/write/end`）。
- **现象**：客户端可不传 `X-Sha256` 头；即便传了，SHA256 仅是完整性校验，无法防伪造。没有公钥签名验证。
- **影响**：在 1.2（无 CSRF）+ 1.1（密码已被记录）任一条件成立时，可远程刷入任意镜像。

### 2.4 OTA 大小依赖客户端自报，缺主动分区上界检查
- **位置**：[src/web/Esp32BaseWeb.inc:1118-1127](src/web/Esp32BaseWeb.inc:1118)
- **现象**：`firmwareSize = strtoul(header("X-Firmware-Size"))`，未在调用 `startUpload` 之前与 `esp_ota_get_next_update_partition()` 的容量做对比。`Update.begin(size)` 内部确实会拒绝过大值，但失败返回的错误信息会反馈「Update.begin failed」，对调试极不友好；`size = 0` 早期分支虽有处理，但伪造极小值同时配合分块写入也可能让状态机进入异常。

### 2.5 NVS 中存的认证 / Wi-Fi 凭据未加密
- **位置**：[src/web/Esp32BaseWeb.inc:260-283](src/web/Esp32BaseWeb.inc:260)。
- **现象**：默认 NVS 不开 flash encryption；通过物理接触读 flash 即可拿明文。
- **影响**：批量回收设备时，凭据外泄。

### 2.6 配置模块共享状态无锁
- **位置**：[src/core/Esp32BaseConfig.cpp:35](src/core/Esp32BaseConfig.cpp:35)（`g_pending[]`）等全局；`setStrDeferred()` 与 `flushNextDue()` 之间，以及 `g_stringScratch[]` 的复用都没有 mutex / critical section。
- **影响**：若用户从 FreeRTOS 任务调用配置 API（手册并未明确禁止），会触发数据竞争 / 内存释放风险。
- **附注**：[src/runtime/Esp32BaseBus.inc:72](src/runtime/Esp32BaseBus.inc:72) 的 `xTaskGetCurrentTaskHandle() != g_loopTask` 在 ISR 中行为不可靠，建议同时检查 `xPortInIsrContext()`。

### 2.7 millis() 49 天回绕
- **位置**：[src/core/Esp32BaseConfig.cpp:341](src/core/Esp32BaseConfig.cpp:341), [:416](src/core/Esp32BaseConfig.cpp:416), [:439](src/core/Esp32BaseConfig.cpp:439), [:450](src/core/Esp32BaseConfig.cpp:450)。
- **现象**：使用 `dueMs = millis() + delayMs` 与 `now >= dueMs` 类比较；当 `dueMs` 跨越 0xFFFFFFFF 时比较结果错误，可能导致延迟刷写在回绕窗口内永远不到期或立即触发。
- **影响**：对长期运行设备（嵌入式典型场景）确定性下降。

### 2.8 OTA 中途断电 / 断网无主动清理 + 恢复回滚未启用
- **位置**：[src/update/Esp32BaseOta.inc:269-308](src/update/Esp32BaseOta.inc:269), [:455-464](src/update/Esp32BaseOta.inc:455)
- **现象**：依赖 `Update` 库自身行为；没有看到 `esp_ota_mark_app_valid_cancel_rollback()` / `esp_ota_mark_app_invalid_rollback_and_reboot()` 与 boot loader rollback 标志的显式接入。一旦上传中断，下次启动表现不稳定。

### 2.9 web auth 可被「全局禁用」绕过
- **位置**：[src/web/Esp32BaseWeb.inc:604-618](src/web/Esp32BaseWeb.inc:604) `ensureAuth()` 在 `g_authEnabled == false` 时直接 `return true`。
- **现象**：任何 OTA / 状态接口在禁用认证后完全开放；`/esp32base/api/ota`、`/esp32base/api/status` 没有任何后备保护。
- **影响**：一次误关认证 → 整个设备暴露。

### 2.10 系统信息端点信息量大，未限速
- **位置**：[src/web/Esp32BaseWeb.inc:793-869](src/web/Esp32BaseWeb.inc:793)（`handleStatus`、`handleChip`）。
- **现象**：返回堆 / flash / 分区 / MAC / 复位原因等指纹信息，无 rate limit；与 1.2、2.9 配合可被未授权扫描。

### 2.11 示例硬编码 4MB 分区表
- **位置**：`examples/basic/platformio.ini:11`、`examples/web_logs_ota/platformio.ini:9` 等。
- **现象**：所有示例都引用 `partitions/esp32-4mb-ota-balanced.csv`；用户拿 ESP32-S3 8MB 板编译可过，但 OTA 容量与实际 flash 不匹配。文档（[docs/05_ota.md](docs/05_ota.md)、[README.md](README.md)）未在示例处提示选型。

### 2.12 API 文档遗漏已实现接口
- **位置**：[docs/03_api.md](docs/03_api.md) vs [src/web/Esp32BaseWeb.h:108](src/web/Esp32BaseWeb.h:108)。
- **现象**：`Esp32BaseWeb::redirectSeeOther()` 已被 `examples/full_demo/src/main.cpp` 使用但文档未录入。

---

## 3. 低危 / 工程债（LOW）

### 3.1 Content-Disposition filename 未对引号转义
- **位置**：[src/web/Esp32BaseWeb.inc:652-657](src/web/Esp32BaseWeb.inc:652)。
- **现象**：上层做了字母 / 数字 / `._-` 的字符白名单（[:378-390](src/web/Esp32BaseWeb.inc:378)），目前不可注入；但若白名单变更，`snprintf(... "filename=\"%s\"")` 不做 escape 是隐患。

### 3.2 Authorization 头长度静默截断
- **位置**：[src/web/Esp32BaseWeb.inc:179](src/web/Esp32BaseWeb.inc:179)（≤180 char）。超长直接走「无凭据」路径，不返 4xx，调试困难。

### 3.3 NTP 同步状态判定弱
- **位置**：[src/network/Esp32BaseNtp.inc:37-55](src/network/Esp32BaseNtp.inc:37)。仅靠 `time(nullptr) > 1700000000` 判断；时钟被人为前调即视为已同步。

### 3.4 NVS 错误处理顺序可优化
- **位置**：[src/core/Esp32BaseConfig.cpp:66-72](src/core/Esp32BaseConfig.cpp:66)，`ESP_ERR_NVS_NOT_FOUND` 与 `!= ESP_OK` 顺序颠倒，会把「不存在」当成「打开失败」记日志。

### 3.5 `setFirmwareInfo()` 未调用时使用 `"app"/"0.0.0"`
- **位置**：[src/Esp32Base.h:50](src/Esp32Base.h:50)。OTA、状态页都会显示这套占位值，会让排障误判。

### 3.6 CHANGELOG 与 library.json version 脱节
- **位置**：[CHANGELOG.md](CHANGELOG.md) 顶部按日期记录；[library.json](library.json) 仍是 `"0.1.0"`，未与日期对齐。后续 PIO Registry 上线后无法回查。

### 3.7 `.gitignore` 越权排除 AI 工作流文件
- **位置**：[.gitignore:54-56](.gitignore:54)，`evaluations/`、`CLAUDE.md`、`.codex-worklog.md` 应放到个人 `.git/info/exclude`，避免污染下游用户。

### 3.8 打包 ignore 规则散落多处
- **位置**：[.libraryignore](.libraryignore)、[.piopmignore](.piopmignore)、[library.json](library.json) `export.exclude`。`design-history/`、`ESP32BASE_DOCUMENTATION_REVIEW*.md` 三处规则各异，存在被某个发行渠道意外打包的可能。

### 3.9 `Esp32BaseFileLog` 文档缺少最小可运行示例
- **位置**：[docs/03_api.md](docs/03_api.md) 第 3.4 节。仅文字描述 `enable()`，未给典型集成代码片段。

### 3.10 webota 进度统计字典未做 reset
- **位置**：[scripts/esp32base_webota.py:287-288](scripts/esp32base_webota.py:287)。`stats = {...}` 在脚本被作为模块多次调用时不会自动重置；当前以脚本方式运行无影响，但若后续被 PIO 后处理脚本反复 import 会有累积。

### 3.11 README 推荐阅读章节编号小毛病
- **位置**：[README.md:128-143](README.md:128)。docs 文件实际为 `00_design.md` ~ `10_known_limitations.md`（11 个），README 列表的标号易误读为 12 项。

---

## 4. 已核实为「误报 / 安全」的代理结论

为避免后续误改，明确记录：

- **`strcpy` 越界（Config.cpp:435）**：目标 buffer 按 `strlen(value)+1` 动态分配，且 value 长度已在第 424 行限制 ≤ 3999，**不会越界**。仅是风格层面建议改 `memcpy(copy, value, len+1)`。
- **`segment` 路径穿越（Esp32BaseWeb.inc:1287）**：在 [:1288](src/web/Esp32BaseWeb.inc:1288) 限长 ≤3 位、[:1294](src/web/Esp32BaseWeb.inc:1294) 拒绝非数字、[:1299](src/web/Esp32BaseWeb.inc:1299) 与 `rotateFiles()` 上界比较，**已做完整边界校验**，不存在越界。
- **OTA `Update.begin(totalSize)` 整数溢出**：`size_t` + Arduino-ESP32 内部分区上界检查可拒绝过大值，**不会有真实溢出**；但缺乏更友好的应用层校验，仍以 2.4 中危记录。
- **配置 `g_stringScratch[CONFIG_STRING_MAX_VISIBLE_LEN + 1]` off-by-one**：实际定义足以容纳 4000 字节并留终止符，未发现越界写入路径。

---

## 5. 验证 / 下一步建议（仅供参考，不实施）

如果用户后续要复核以上结论，可执行：

- `grep -n "password=%s" src/web/Esp32BaseWeb.inc` 直接看 1.1
- `grep -nE "csrf|SameSite|Origin|Referer" src/web/Esp32BaseWeb.inc` 验 1.2 仍为空
- 运行 `python scripts/esp32base_webota.py --help` 与代码 330-333 行交叉看 1.3
- 用 `pio run -e <board>` 在 ESP32-S3 8MB 板编译 [examples/basic](examples/basic) 复现 2.11
- `git grep -n "redirectSeeOther"` 复核 2.12 文档遗漏

---

## 6. 关键审计文件清单（按优先级）

| 优先 | 文件 | 主要问题 |
|---|---|---|
| 1 | [src/web/Esp32BaseWeb.inc](src/web/Esp32BaseWeb.inc) | 1.1 / 1.2 / 2.1 / 2.9 / 2.10 |
| 2 | [src/update/Esp32BaseOta.inc](src/update/Esp32BaseOta.inc) | 2.3 / 2.4 / 2.8 |
| 3 | [scripts/esp32base_webota.py](scripts/esp32base_webota.py) | 1.3 / 3.10 |
| 4 | [src/core/Esp32BaseConfig.cpp](src/core/Esp32BaseConfig.cpp) | 2.6 / 2.7 / 3.4 |
| 5 | [src/network/Esp32BaseWiFi.inc](src/network/Esp32BaseWiFi.inc) | 2.2 |
| 6 | [docs/03_api.md](docs/03_api.md) / 各 examples | 2.11 / 2.12 / 3.9 / 3.11 |
| 7 | 打包 / 忽略 / library.json | 3.6 / 3.7 / 3.8 |

---

## 总结

发现 **3 个高危**（Web 凭据明文泄露、缺 CSRF、webota 长上传超时）、**12 个中危**（默认弱口令、开放 AP、OTA 签名缺失、配置无锁、49 天回绕、auth-disable 后端口全开、文档/示例分区表选型不当等）、**11 个低危**（信息泄露细节、文档脱节、打包配置、轻微工程债）。前两项高危属于 Web 安全模型本身的设计缺口，需要在设计层面权衡（CSRF token + 日志脱敏）；webota 超时则是脚本侧一处具体修复点。

**用户已声明不做修改，本评估到此结束**；如需把任意一条转为修复任务，请明确告知后再切到实现模式。
