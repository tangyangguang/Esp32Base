# Application Event Log Design

日期：2026-05-31

## 背景

Esp32Base 已经提供系统层能力：启动、WiFi、NTP、OTA、LittleFS、Health、FileLog、系统诊断和 Web 管理页。业务项目还需要另一类记录：应用事件。

应用事件用于让用户和维护者理解业务行为，例如某个计划为什么未执行、某个外部 API 为什么做出决策、某个业务告警为什么被清除。它不是 Esp32Base 自身的系统日志，也不是调试日志，因此不能混入 `Esp32BaseFileLog`、WiFi/OTA/NTP/Health 诊断或 `/esp32base/logs` 页面。

但应用事件的固定容量、环形覆盖、分页读取、Web/API 查看、导出、清空、escape、鉴权和写入失败处理是通用基础能力。该能力应沉淀到 Esp32Base，由业务项目只负责事件语义和写入时机。

## 目标

- 提供一个默认关闭、显式启用的通用应用事件日志模块。
- 不包含任何灌溉或其他业务领域枚举。
- 允许应用写入结构化事件，包含时间、等级、来源、类型、原因、对象、子码、数值和短文本。
- 固定容量，环形覆盖，分页读取，长期运行可靠。
- 使用 LittleFS 固定文件存储，不为每条事件写 NVS。
- Web 内置页面和 API 能查看、分页、导出和清空应用事件。
- 内置页面和导航明确区分 `Logs` 与 `App Events`。
- 应用恢复出厂或清空业务记录时可按需调用 clear。
- FS 未就绪、文件损坏、容量不足、写入失败和断电恢复都有明确语义。

## 非目标

- 不解释业务事件含义。
- 不注册业务枚举、业务 label、单位表或页面文案映射。
- 不把应用事件写入 FileLog，也不把 FileLog 内容导入应用事件。
- 不自动转发 Esp32Base 的 WiFi、OTA、NTP、启动、Health 系统事件。
- 不提供全文搜索、复杂筛选、索引、多租户权限或云端同步。
- 不把 `object` 或 `text` 当作大 payload 存储。

## 编译与分层

新增宏：

```cpp
#define ESP32BASE_ENABLE_APP_EVENTS 0
#define ESP32BASE_APP_EVENT_LOG_CAPACITY 1024
```

默认关闭。业务项目需要时显式开启：

```ini
build_flags =
  -D ESP32BASE_ENABLE_APP_EVENTS=1
```

依赖关系：

- `ESP32BASE_ENABLE_APP_EVENTS` 需要 `ESP32BASE_ENABLE_FS`。
- Web 查看页只在 `ESP32BASE_ENABLE_WEB && ESP32BASE_ENABLE_APP_EVENTS` 时注册。
- 该模块属于 Runtime/FS 能力，不进入 Core。
- 记录时间由 `Esp32Base` 高层在可用时注入 NTP 快照；Runtime 存储模块不直接依赖 Network 层。

Profile 策略：

- 7 个 profile 数量不增加。
- 不随 `FULL` 默认开启，避免默认占用 100-200 KiB FS。
- 应用项目显式启用后承担存储和路由成本。

## 记录实体

记录实体应偏向“结构化事实”，而不是“小型业务记录”。机器语义放在 token、code、object 和 value 中；短文本只做人读补充。

推荐固定布局：

```cpp
struct Esp32BaseAppEventRecord {
    uint32_t magic;
    uint32_t id;
    uint32_t epochSec;
    uint32_t bootId;
    uint32_t uptimeSec;

    int32_t value1;
    int32_t value2;
    int32_t value3;

    uint16_t code;
    uint8_t level;
    uint8_t flags;
    uint8_t valueMask;
    uint8_t reserved;
    uint16_t crc16;

    char source[12];
    char type[24];
    char reason[24];
    char object[56];
    char text[32];
};

static_assert(sizeof(Esp32BaseAppEventRecord) == 188);
```

字段语义：

| 字段 | 语义 |
| --- | --- |
| `magic` | 单条记录有效性标记。 |
| `id` | 单调递增事件 ID；溢出后跳过 0。 |
| `epochSec` | 可信 NTP 时间；未同步时为 0。 |
| `bootId` | 启动会话 ID，复用 Esp32Base NTP/System 启动会话语义。 |
| `uptimeSec` | 启动后秒数，使用 64-bit uptime 来源换算，不受 `millis()` 回卷影响。 |
| `value1..3` | 应用定义的关键数值槽。 |
| `code` | 应用定义的子码、结果码或枚举码；0 表示不用。 |
| `level` | `INFO`、`WARN`、`ERROR`。 |
| `flags` | 记录状态，例如 `TIME_SYNCED`、`TEXT_TRUNCATED`。 |
| `valueMask` | bit0/1/2 表示 `value1/2/3` 是否有效，避免把缺失值误读为 0。 |
| `crc16` | 单条记录完整性校验。 |
| `source` | 来源类别 token，例如 `web`、`api`、`button`、`scheduler`、`external`。 |
| `type` | 事件类型 token，例如 `schedule.skipped`、`watering.no_flow`。 |
| `reason` | 原因 token，例如 `manual_skip`、`disabled`、`protection`。 |
| `object` | 业务对象引用，例如 `zone:1`、`plan:morning-a`、`api:weather/home`。 |
| `text` | 很短的人读补充说明。 |

### 字段大小依据

- `source[12]` 保持小。它是来源类别，不是详情。
- `type[24]` 和 `reason[24]` 足够表达可读 token，避免业务被迫使用难懂缩写。
- `object[56]` 是最大的字符串字段，用于业务对象引用。它可以比 `text` 大，因为对象引用对查询和定位更重要。
- `text[32]` 刻意较小，只用于短说明；完整业务上下文应由业务记录或业务页面解释。
- `value1..3` 是合理上限。三个数值可覆盖测量值/阈值/窗口、目标/实际/差值、旧值/新值/影响数量。需要 4 个以上数值时，通常已经是业务记录，不应塞进通用事件日志。
- `code` 单独存在，避免业务把枚举结果硬塞进 `value1`。
- `valueMask` 必须存在，避免无法区分“值为 0”和“未提供值”。

### 输入规则

- `source`、`type`、`reason` 和 `object` 推荐使用 ASCII token。
- token 允许字母、数字、`.`、`_`、`-`、`:`、`/`；其他字符按非法输入处理。
- `source` 和 `type` 必填；为空、非法或超出字段容量时 append 失败。
- `reason` 和 `object` 可为空；非空时必须合法且完整放入字段，超出容量时 append 失败，避免机器语义被截断后误读。
- `text` 允许 UTF-8 可见文本，但过滤 CR/LF 和控制字符；超出容量时按有效 UTF-8 边界截断并设置 `TEXT_TRUNCATED` flag。
- 基础库不校验业务 token 是否存在于某个枚举表。

## 容量

默认容量：

```cpp
ESP32BASE_APP_EVENT_LOG_CAPACITY=1024
```

容量估算：

| 容量 | 记录区占用 | 评价 |
| ---: | ---: | --- |
| 512 | 94 KiB | 保守，可能偏少。 |
| 768 | 141 KiB | 均衡。 |
| 1024 | 188 KiB | 推荐默认。 |
| 1280 | 235 KiB | 适合高频事件项目，不建议默认。 |

仓库 4MB 默认分区表中 LittleFS 约 1408 KiB。默认 FileLog 为 128 KiB；应用显式启用 App Events 后，1024 条应用事件约 188 KiB。两者合计约 316 KiB，仍保留约 1.1 MiB 给业务文件、导入数据和维护空间。由于 App Events 默认关闭，这个默认容量只影响明确选择该能力的业务项目。

`ESP32BASE_APP_EVENT_LOG_CAPACITY` 建议限制在 `64..2048`。超过 2048 条会让 Web 分页和 FS 扫描场景更重，应由业务项目自行评估。

## 存储格式

使用一个固定 LittleFS 文件：

```text
/app/events.bin
```

文件结构：

```text
Header A
Header B
Record[capacity]
```

Header 包含：

- `magic`
- `version`
- `recordSize`
- `capacity`
- `head`
- `count`
- `nextId`
- `sequence`
- `crc32`

双 header 交替写入。启动时选择 CRC 正确且 `sequence` 最新的 header。若两个 header 都无效，模块进入 fault 状态，不自动清空旧文件。

append 顺序：

1. 校验输入并构造 `Esp32BaseAppEventRecord`。
2. 写入当前 head 对应记录槽。
3. 写后读取该槽并校验 `magic`、`id`、`crc16`。
4. 更新内存中的 `head/count/nextId/sequence`。
5. 写入另一个 header 并校验 header CRC。

断电语义：

- 如果断电发生在记录写入前，旧 header 仍有效，旧数据不受影响。
- 如果断电发生在记录写入后、header 写入前，最多丢失本条事件。
- 如果断电发生在 header 写入中，另一个 header 仍可恢复。
- 如果两个 header 都损坏，模块不猜测扫描重建，避免误读旧槽；Web 显示 fault，用户可导出文件或清空。

clear 语义：

- `clear()` 重建固定文件，写入空 header。
- 默认保留 `nextId` 继续递增，避免清空后 ID 倒退造成外部系统混淆。
- 如果业务希望 ID 归 1，可在未来单独评估显式 API；第一版不提供。

错误语义：

- FS 未 ready：`begin()` 返回 false，`isReady=false`。
- 固定文件创建失败：进入 fault，`append()` 返回 false。
- 记录写入或校验失败：进入 fault，后续 `append()` 返回 false，避免继续冲击异常 FS。
- `readLatest()` 遇到单条记录 CRC 失败时跳过该记录并继续读其他记录，返回值仍表示读取流程是否完成。

## 公开 API

```cpp
class Esp32BaseAppEventLog {
public:
    enum Level : uint8_t {
        LEVEL_INFO = 1,
        LEVEL_WARN = 2,
        LEVEL_ERROR = 3
    };

    enum ValueMask : uint8_t {
        VALUE1 = 1 << 0,
        VALUE2 = 1 << 1,
        VALUE3 = 1 << 2
    };

    struct Event {
        Level level;
        const char* source;
        const char* type;
        const char* reason;
        const char* object;
        uint16_t code;
        int32_t value1;
        int32_t value2;
        int32_t value3;
        uint8_t valueMask;
        const char* text;
    };

    struct StoredEvent {
        uint32_t id;
        uint32_t epochSec;
        uint32_t bootId;
        uint32_t uptimeSec;
        Level level;
        uint8_t flags;
        uint8_t valueMask;
        uint16_t code;
        int32_t value1;
        int32_t value2;
        int32_t value3;
        char source[12];
        char type[24];
        char reason[24];
        char object[56];
        char text[32];
    };

    using ReadCallback = void (*)(const StoredEvent& event, void* user);

    static bool begin();
    static bool append(const Event& event);
    static bool readLatest(uint16_t offset, uint16_t limit, ReadCallback cb, void* user = nullptr);
    static bool clear();

    static bool isReady();
    static bool faulted();
    static const char* lastError();
    static uint16_t count();
    static uint16_t capacity();
    static uint32_t nextId();
    static const char* path();
};
```

API 约束：

- `append()` 必须在 loop/system task 中调用，不保证跨任务线程安全。
- 高频实时任务不得直接写 App Events，应投递到 loop/system task。
- `source` 和 `type` 必填；其他字符串可为空。
- `source/type/reason/object` 属于机器语义字段，非法或超长时 append 失败，不做截断。
- `valueMask` 决定哪些 value 有效。未设置 bit 的 value 输出为 `null`。
- `append()` 失败时业务应记录普通 `ESP32BASE_LOG_W` 或进入降级提示，不应无限重试。

## 时间语义

启用 NTP 时，`Esp32Base` 在开始或 handle 阶段给 App Events 注入统一时间快照：

- `epochSec` 仅在可信同步时写入。
- 未同步时 `epochSec=0`。
- `bootId` 和 `uptimeSec` 始终尽量写入。

基础库不自动回填历史事件时间。若未来需要回填，只能回填当前 boot 的事件，且需要单独设计扫描与重写策略；第一版不做。

## Web 与 API

启用 Web 和 App Events 时，基础库注册以下路径：

- `GET /esp32base/app-events`
- `GET /esp32base/api/app-events?offset=0&limit=50`
- `GET /esp32base/app-events.csv`
- `POST /esp32base/app-events/clear`

页面行为：

- 页面标题为 `Application Events`。
- 使用记录列表 baseline，展示时间、等级、source、type、reason、object、code、value 和 text。
- `epochSec=0` 时显示 `boot <bootId> + <uptimeSec>s`，不伪造日期。
- value 未设置 mask 时显示 `-` 或 JSON `null`。
- 顶部说明这是应用结构化事件，不是 Esp32Base 系统日志。
- 空状态说明“应用尚未写入事件”。
- fault 状态显示 WARN notice，说明 FS 未就绪、文件损坏或写入故障。
- 清空按钮使用 `dangerpanel`、浏览器确认、POST、同源检查、Basic Auth 和 `POST -> 303 -> GET`。

导航：

- 内置系统导航增加显式入口 `App Events`。
- `Logs` 仍指向 `/esp32base/logs`，只展示 FileLog。
- `App Events` 指向 `/esp32base/app-events`，只展示应用事件。
- `Esp32BaseWeb::BuiltinPage` 增加 `BUILTIN_APP_EVENTS`，允许业务项目本地化标签，例如“应用事件”。
- `App Events` 只在 `ESP32BASE_ENABLE_APP_EVENTS=1` 时出现。

JSON 示例：

```json
{
  "ready": true,
  "faulted": false,
  "count": 2,
  "capacity": 1024,
  "offset": 0,
  "limit": 50,
  "events": [
    {
      "id": 12,
      "epochSec": 0,
      "bootId": 44,
      "uptimeSec": 82,
      "level": "warn",
      "source": "scheduler",
      "type": "schedule.skipped",
      "reason": "manual_skip",
      "object": "plan:morning-a",
      "code": 3,
      "value1": 1,
      "value2": null,
      "value3": null,
      "text": "skipped by user"
    }
  ]
}
```

CSV 导出字段：

```text
id,epochSec,bootId,uptimeSec,level,source,type,reason,object,code,value1,value2,value3,text
```

所有 HTML、JSON 和 CSV 输出必须复用现有 escape helper。

## 与业务项目的关系

业务项目负责：

- 何时写事件。
- `source/type/reason/object/code/value/text` 的命名和解释。
- 业务页面中的本地化文案和含义解释。
- 恢复出厂或清业务记录时是否调用 `Esp32BaseAppEventLog::clear()`。

Esp32Base 负责：

- 存储结构。
- 环形覆盖。
- 断电恢复。
- 分页读取。
- Web/API/CSV。
- 清空操作安全边界。
- FS 故障和 fault 状态表达。

灌溉项目接入示例：

```cpp
Esp32BaseAppEventLog::append({
    Esp32BaseAppEventLog::LEVEL_WARN,
    "scheduler",
    "schedule.skipped",
    "manual_skip",
    "plan:morning-a",
    3,
    1,
    0,
    0,
    Esp32BaseAppEventLog::VALUE1,
    "skipped by user"
});
```

该例只使用通用 token，不把灌溉枚举加入基础库。

## 文档更新范围

实现时必须同步更新：

- `README.md`：能力定位、启用宏、默认容量、最小接入示例。
- `CHANGELOG.md`：新增 API、业务用途、边界和推荐接入方式。
- `docs/01_architecture.md`：Runtime/Web 分层。
- `docs/02_profiles.md`：默认关闭、依赖 FS、裁剪规则。
- `docs/03_api.md`：完整 API、实体、容量、错误语义。
- `docs/04_web.md`：页面/API 路径、导航入口、鉴权、POST 清空。
- `docs/06_memory_budget.md`：188B 记录、1024 条约 188 KiB、容量评估。
- `docs/07_diagnostics.md`：验证矩阵和故障场景。
- `docs/09_release_checklist.md`：发布前检查项。
- `examples/full_demo/README.md` 或示例说明：写入、分页、导出、清空演示。

## 验证策略

静态验证：

- 默认 `ESP32BASE_ENABLE_APP_EVENTS=0`。
- 开启 App Events 但关闭 FS 时编译报错。
- 非 Web profile 不链接 WebServer 路由。
- Web 内置导航启用后包含 `App Events`，未启用时不出现。
- `/esp32base/logs` 不引用 App Events。
- 文档包含实体大小、默认容量、默认关闭和页面/API 路径。

示例构建：

- `examples/full_demo -e esp32_full` 显式启用 App Events。
- 至少覆盖 FULL profile。
- 裁剪检查确认 CORE/NET 未引入 App Events、LittleFS 或 Web 路由。

存储验证：

- FS 未 ready 时 begin/append/read/clear 的返回值正确。
- 首次创建固定文件成功。
- 1024 条重复写入后 count 不超过 capacity，最新事件覆盖最旧事件。
- 重启后 header 恢复，读取顺序正确。
- 模拟 header A 损坏时可用 header B 恢复。
- 模拟两个 header 都损坏时进入 fault，不自动清空。
- 模拟单条记录 CRC 错误时跳过该记录。
- clear 后 count 为 0，nextId 继续递增。

Web/API 验证：

- 未认证访问返回 401。
- `GET /esp32base/app-events` HTML escape 正确。
- `GET /esp32base/api/app-events` JSON escape 正确。
- `GET /esp32base/app-events.csv` CSV escape 正确。
- `GET /esp32base/app-events/clear` 不存在或拒绝副作用。
- `POST /esp32base/app-events/clear` 要求认证和同源检查。
- 清空成功后 303 回到 GET 页面，刷新不重复提交。
- `Logs` 页面不显示应用事件。

实机验证：

- 长期重复写入不触发 watchdog。
- FS 满或文件损坏时进入 fault，不无限重试写 flash。
- Web 查看大页、CSV 导出和分页读取期间正常 yield/feed watchdog。
- OTA、restart、deep sleep 前不要求额外 flush，因为 append 是同步确认写入。

## 设计结论

采用默认关闭的 `Esp32BaseAppEventLog`。记录固定为 188 bytes，默认 1024 条，约 188 KiB。实体保留 3 个数值槽、`code`、`valueMask`、较大的 `object` 和较短的 `text`，既能覆盖多数业务事件，又避免基础库变成业务数据平台。

Web 层提供 `/esp32base/app-events` 显式入口和 API/CSV/清空能力，导航中与 `Logs` 并列但语义分离。业务项目负责事件命名和解释，Esp32Base 只负责可靠存储、通用展示和安全维护操作。
