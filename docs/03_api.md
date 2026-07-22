# API 契约

本文记录当前公开 API 形态和调用边界。

## 1. 头文件

统一公开入口：

```cpp
#include <Esp32Base.h>
```

不提供 `Esp32BaseAliases.h` 或 `Log`、`Config`、`Web` 等全局短别名。用户应使用完整类名，避免和 Arduino、第三方库或应用自己的全局符号冲突。

## 2. Esp32Base

职责：

- 固件信息。
- hostname。
- profile 信息。
- begin / handle。
- 启动诊断和资源日志。

建议 API：

```cpp
class Esp32Base {
public:
    static bool begin();
    static void handle();

    static void setFirmwareInfo(const char* name, const char* version, const char* build = nullptr);
    static const char* firmwareName();
    static const char* firmwareVersion();
    static const char* firmwareBuild();

    static const char* hostname();
    static const char* defaultHostname();
    static bool isValidHostname(const char* hostname);

    static const char* profileName();
    static bool isReady();
    static const char* lastError();

    static void logStartupConfig();
    static void logResources();
};
```

约定：

- 默认 hostname 由编译期宏 `ESP32BASE_DEFAULT_HOSTNAME` 指定；未设置时为 `"esp32base"`。
- 应用代码不提供 hostname setter，避免运行期半生效切换造成 mDNS、ArduinoOTA 和 Web 识别不一致。
- `begin()` 在 Config 初始化后读取 `eb_sys.hostname`；合法持久化值覆盖构建默认值，非法值记录 WARN 并忽略。
- hostname 校验规则：1-32 位小写字母、数字、短横线；不能以短横线开头或结尾；不允许 `.` 或 `.local`。
- Web/API 保存 hostname 只写入 `eb_sys.hostname`，不修改当前运行时 hostname；重启后完整生效。
- `factoryReset()` 清理 `eb_sys.hostname` 后，重启恢复 `ESP32BASE_DEFAULT_HOSTNAME`。
- 应用应显式调用 `setFirmwareInfo()`；未设置时状态页和日志会显示库默认固件名/版本，仅用于开发占位。
- WiFi STA 连接前会把当前 hostname 应用为 DHCP client hostname；mDNS 和 ArduinoOTA 启动时也读取当前 hostname。运行期不做 hostname 热切换。
- `lastError()` 返回最近一次 begin 或关键模块启动失败原因；无错误时返回空字符串，不返回 `nullptr`。

## 3. Esp32BaseLog

### 3.1 编译期级别

日志级别：

```cpp
#define ESP32BASE_LOG_NONE    0
#define ESP32BASE_LOG_ERROR   1
#define ESP32BASE_LOG_WARN    2
#define ESP32BASE_LOG_INFO    3
#define ESP32BASE_LOG_DEBUG   4
#define ESP32BASE_LOG_VERBOSE 5
```

默认：

```cpp
ESP32BASE_LOG_LEVEL=ESP32BASE_LOG_INFO
```

关闭级别的日志不得保留 format 字符串到 `.rodata`。

### 3.2 日志宏

```cpp
ESP32BASE_LOG_E(tag, fmt, ...)
ESP32BASE_LOG_W(tag, fmt, ...)
ESP32BASE_LOG_I(tag, fmt, ...)
ESP32BASE_LOG_D(tag, fmt, ...)
ESP32BASE_LOG_V(tag, fmt, ...)
```

tag 可以为 `nullptr`，实现应退化为空字符串或默认 tag。

### 3.3 运行时 API

```cpp
class Esp32BaseLog {
public:
    enum Level : uint8_t {
        NONE = 0,
        ERROR = 1,
        WARN = 2,
        INFO = 3,
        DEBUG = 4,
        VERBOSE = 5
    };

    using Sink = void (*)(Level level, const char* tag, const char* message);
    using LineSink = void (*)(Level level, const char* tag, const char* message, const char* line);
    using TimeProvider = const char* (*)();

    static bool begin(uint32_t baud = ESP32BASE_LOG_BAUD);
    static void setRuntimeLevel(Level level);
    static Level runtimeLevel();
    static void setSerialLevel(Level level);
    static Level serialLevel();
    static void setSink(Sink sink);
    static void setInternalLineSink(LineSink sink);
    static void setTimeProvider(TimeProvider provider);

    static void write(Level level, const char* tag, const char* fmt, ...);
    static void formatBytes(uint64_t bytes, char* out, size_t len);
    static void formatMillis(uint32_t ms, char* out, size_t len);
    static void formatUptime(uint32_t ms, char* out, size_t len);
    static void formatUptime64(uint64_t ms, char* out, size_t len);
};
```

运行时级别只能进一步过滤，不能突破编译期上限。

`runtimeLevel()` 控制日志是否生成并分发给 sink / FileLog；`serialLevel()` 只控制 Serial 输出。量产设备需要关闭串口但保留系统诊断日志时，不要把 `ESP32BASE_LOG_LEVEL` 编译为 `ESP32BASE_LOG_NONE`，而应保持编译期等级为系统诊断日志需要记录的最高等级，再在运行期关闭 Serial：

```cpp
Esp32BaseLog::setSerialLevel(Esp32BaseLog::NONE);
Esp32BaseFileLog::setMode(Esp32BaseFileLog::ERROR);
```

如果 `ESP32BASE_LOG_LEVEL=ESP32BASE_LOG_NONE`，日志宏会在编译期变为空语句，Serial、sink 和 FileLog 都不会收到日志。

输出目标：

- Serial。
- 可选 sink callback。
- Runtime/FS profile 中由 `Esp32BaseFileLog` 提供系统诊断日志；Core Log 本身不依赖 FS。

日志格式要求：

- 输出应按启动顺序、状态迁移顺序和错误链路组织，方便串口观察系统正在做什么。
- 默认格式包含 uptime、level、tag、message；对齐和间隔应保持稳定，便于人工扫描。
- `DEBUG` 编译级别下，启动流程应输出模块初始化顺序和 Web/NTP/mDNS/OTA 等延迟启动原因；默认 `INFO` 构建不输出这些开发诊断日志。
- message 按调用方传入内容输出，不规整 CR/LF；需要单行日志时由调用方传入单行 message。
- 日志和内置页面中的大数字字节数必须使用人性化显示，例如 `1.00 MB`，不重复输出原始 bytes。
- `formatBytes()` 采用二进制单位，`1 KB = 1024 bytes`，`1 MB = 1024 KB`。
- 状态/API JSON 可保留 raw `bytes` 数值，同时提供 `human` 字段用于人性化展示。
- WiFi/Web Auth 日志不得输出密码值；可输出 SSID、用户名、来源、结果和 `password_set` 这类状态字段。
- sink callback 在日志调用现场同步执行，用户 sink 不得长时间阻塞。

### 3.4 Esp32BaseFileLog

`Esp32BaseFileLog` 是系统诊断日志的实现/API 名称，面向开发、维护和运维排障，不是应用业务事件接口。仅在 `ESP32BASE_ENABLE_FILELOG=1` 时可用，依赖 `Esp32BaseFs`。默认文件为 `/esp32base/logs/system.log`，默认 Web 标签为 `System Logs`，`4 × 32KB` 轮转，默认模式 ERROR。默认 ERROR 是低 Flash 磨损策略：全功能固件在没有业务数据、应用事件或显式调试日志需求时，只把错误级系统诊断写入 LittleFS。运行时系统诊断日志模式只支持 OFF、ERROR、WARN、INFO；DEBUG/VERBOSE 不作为文件日志模式。WARN/INFO 适合现场排查时显式开启；INFO 模式使用 1KB / 2s 缓存，不做节流，会把启动、联网、OTA、性能提示和维护动作等低优先级日志写入 LittleFS，不建议作为长期量产默认。

```cpp
class Esp32BaseFileLog {
public:
    enum Mode : uint8_t {
        OFF = ESP32BASE_FILELOG_MODE_OFF,
        ERROR = ESP32BASE_FILELOG_MODE_ERROR,
        WARN = ESP32BASE_FILELOG_MODE_WARN,
        INFO = ESP32BASE_FILELOG_MODE_INFO
    };

    static bool begin();
    static bool setMode(Mode mode);
    static bool flush();
    static bool clear();
    static void handle();
    static bool isEnabled();
    static bool faulted();
    static Mode mode();
    static const char* modeName();
    static const char* path();
    static uint32_t maxBytes();
    static uint8_t rotateFiles();
    static uint32_t size();
    static uint32_t segmentSize(uint8_t index);
    static bool bufferEnabled();
    static uint16_t bufferSize();
    static uint16_t bufferUsed();
    static uint32_t flushIntervalMs();
};
```

`isEnabled()` 表示当前运行期是否还在写系统诊断日志；`mode()` 表示配置模式。FS 满、文件损坏或底层写失败时，FileLog 会进入运行期故障保护：`faulted()` 返回 true，`mode()` 仍保留用户配置，`isEnabled()` 返回 false，避免后续 WARN/ERROR 继续冲击异常文件系统。Web 显示为 `write fault`，表示新日志写入已停，不表示已有日志一定不可读取。若 `mode()!=OFF`、`isEnabled()==false` 且 `faulted()==false`，Web 显示为 `unavailable`，表示系统日志配置开启但 FS 未 ready、路径/容量前置条件或初始化状态暂时不满足；`disabled` 只表示模式为 OFF。清理/格式化文件系统或重新保存 FileLog 模式后可重试启用。

LittleFS mount failed 时不会自动格式化；`Esp32BaseFs::begin()` 只返回 false，不 halt。格式化会删除该分区全部文件，只允许通过明确的维护动作调用 `Esp32BaseFs::format()` 或 Web System 页格式化入口触发，启动路径不得隐式清空业务记录、校准数据或用户上传文件。

最小可运行示例：

```cpp
#define ESP32BASE_PROFILE ESP32BASE_PROFILE_RUNTIME
#include <Esp32Base.h>

void setup() {
    Esp32Base::begin();
    Esp32BaseFileLog::setMode(Esp32BaseFileLog::INFO);
    ESP32BASE_LOG_I("app", "boot");
}

void loop() {
    Esp32Base::handle();
}
```

### 3.5 Esp32BaseRecordStore

仅在 `ESP32BASE_ENABLE_RECORD_STORE=1` 时可用，依赖 FS 和 Time。它为固定长度、不透明业务负载提供多实例可靠记录存储；浇水、开关门、喂食等业务分别定义自己的字节布局和文件预算，基础库不理解字段语义。

每个实例使用：

```cpp
Esp32BaseRecordStore::StoreDefinition definition;
definition.recordTypeName = "door-opening";
definition.storeVersion = 1;
definition.payloadSizeBytes = 8;
definition.maximumStoreBytes = 32UL * 1024UL;
definition.minimumFileSystemFreeBytes = 8UL * 1024UL;
```

文件路径由基础库生成：

```text
/esp32base/records/<recordTypeName>.v<storeVersion>/
  control.bin
  <firstRecordId>.seg
```

`recordTypeName` 只允许小写字母、数字和内部连字符，最大32字符。`storeVersion=0`、空负载或不足以容纳一条记录的预算无效。版本同时进入目录名和控制头/段头，但不在每条记录中重复。不同业务类型、不同版本使用独立目录和独立ID序列。

固定槽位采用明确的小端编码：

| 偏移 | 字段 | 大小 |
| ---: | --- | ---: |
| 0 | recordId | 4 |
| 4 | completedEpochSec | 4 |
| 8 | completedBootId | 4 |
| 12 | completedUptimeSec | 4 |
| 16 | durationSec | 4 |
| 20 | 业务payload | N |
| 20 + N | CRC32 | 4 |

因此：

```text
slotSizeBytes = payloadSizeBytes + 24
```

`maximumStoreBytes` 是包含128字节控制文件、32字节段头和所有槽位的最大逻辑预算，不预分配大文件。基础库按预算自动选择LittleFS CTZ有效段上限，使预计段数不超过33；初始化日志和 `StoreStatus.capacity` 给出该固定定义下的估算容量。容量轮换按完整最老段淘汰，因此实际记录数会在“估算容量减去一个段容量”到估算容量之间波动。

`control.bin` 包含两个64字节带CRC的控制头，普通追加不更新它。段按首个记录ID命名，顺序追加到当前段；新段先完整创建和验证 `.tmp`，再重命名为 `.seg`。达到预算后删除最老完整段，不搬移或重写其他段。启动或 `reload()` 批量扫描有限段恢复运行态，最新分页按段倒序读取，不能按槽位反复打开文件。正式控制文件定义不匹配、段范围重叠或目录结构未知时进入结构故障，绝不自动重写。

核心API：

```cpp
class Esp32BaseRecordStore {
public:
    struct StoreDefinition;
    struct RecordStartTime;
    struct RecordTiming;
    struct RecordView;
    struct RecordMetadata;
    struct StoreStatus;

    bool begin(const StoreDefinition&);
    bool reload();

    bool captureStartTime(RecordStartTime&);
    bool appendInstant(const uint8_t* payload, size_t payloadBytes);
    bool appendCompleted(const RecordStartTime&, const uint8_t* payload, size_t payloadBytes);

    bool readLatest(uint32_t offset, uint32_t limit,
                    uint8_t* scratch, size_t scratchBytes,
                    ReadCallback callback, void* user = nullptr);
    RecordReadResult readById(uint32_t recordId,
                              uint8_t* payloadOut, size_t payloadBytes,
                              RecordMetadata& recordOut);

    bool readStatus(StoreStatus&) const;
    bool clear();
};
```

时间约定：

- `appendInstant()` 自动保存完成快照，`durationSec=0`。
- 实际业务动作开始时调用 `captureStartTime()`，动作得到正常或异常结果后调用 `appendCompleted()`。
- 开始和完成必须属于同一个boot；跨boot调用被拒绝。
- 有可信RTC/NTP时间时保存epoch，否则保存 `bootId + uptimeSec`；`resolveCompletedEpoch()` 和 `resolveStartedEpoch()` 只在能够可靠解析时返回真实时间。
- 计划时间不是通用实际动作时间；业务确实需要时放入自己的payload。

ID和轮换：

- 每个“记录类型 + storeVersion”文件使用独立32位ID，从1开始。
- 自动淘汰和逻辑清空都不重置ID；到达 `UINT32_MAX` 后停止追加，不回卷。
- 如果断电时已经写入了可识别的下一ID但整条记录未完成，该ID会被保留为空洞而不重复使用；分页和按ID读取仍不会返回不完整记录。
- 段文件名和段内固定偏移共同定位ID，不建立字段索引。
- 容量满后删除最老完整段，不覆盖或搬移其他段；淘汰粒度是一段而不是一条。
- 不支持单条删除、更新、字段查询、事务或可变长度记录。

失败和状态：

- CRC错误或不完整槽位不会返回；单槽损坏进入 `Degraded`，其他有效记录仍可读取。
- 写入或写后验证失败进入 `WriteFault`，已有记录仍可读取；应用可在排除原因后显式 `reload()`。
- 双控制头无效、定义不匹配、段范围重叠或目录结构未知进入 `StructuralFault`，不自动清空。
- `minimumFileSystemFreeBytes` 在创建和追加前保护业务指定的LittleFS余量；0表示不附加该限制。
- `StoreStatus` 返回状态、错误、记录数、估算容量、损坏数、最早/最新/下一个ID、槽位大小、段数、段上限、当前/最大逻辑存储字节以及LittleFS总量/使用量/剩余量。
- `clear()` 先通过双控制头提交新的可见边界，再尽力删除旧段；删除失败也不会让旧记录重新可见。它不是安全擦除，调用方必须先取得用户明确确认。

RecordStore构造函数不访问FS，也不会自动登记到Web。业务在 `Esp32Base::begin()` 成功后调用各实例的 `begin()`；同一路径只能有一个活动实例。API不提供mutex，同一Store的begin/reload/append/read/clear必须由业务串行调用，最简单的做法是集中在同一loop/system task；业务也可以使用自己的等效串行化机制。读取回调中的payload只在本次回调期间有效，且不得在回调中重入同一Store。

启用Web并希望由基础库System页统一展示、清空和格式化后恢复业务记录时，每个当前版本Store只需在首次 `begin()` 调用完成后登记一次。重复登记同一对象是幂等的。即使 `begin()` 因已有Store损坏而返回失败，只要定义已经建立，登记仍可成功并展示故障；无效定义会使两者都失败。应用应分别检查两个返回值：

```cpp
const bool storeReady = wateringRecords.begin(wateringDefinition);
const bool storeRegistered = Esp32BaseWeb::registerBusinessRecordStore(wateringRecords);
if (!storeRegistered) {
    ESP32BASE_LOG_E("watering", "record_store_web_registration_failed");
}
if (!storeReady) {
    ESP32BASE_LOG_E("watering", "record_store_begin_failed error=%s",
                    wateringRecords.lastErrorReason());
}
```

登记只保存对象指针且当前不提供反登记，因此对象从登记成功起必须持续有效到设备重启；全局对象、静态对象或由应用长期持有的成员对象都可以，不要求采用某一种代码组织方式。只登记当前实际使用的版本；未登记历史版本不属于 `Clear Business Records`。清空前基础库预检全部已登记Store；任一Store未初始化或结构故障时一个都不清。执行中发生I/O失败时停止后续Store并报告已完成数量，不能承诺多个独立文件之间的事务原子性。成功清空会直接更新原对象的可见边界、计数、状态和 `nextRecordId`，应用继续使用该对象即可，无需重新 `begin()`，也不得自行把ID重置为1。App Events使用独立入口，不进入业务Store登记表。

`appendCompleted()` 是同步持久化操作：返回成功前会顺序追加当前段、执行LittleFS flush并做写后验证；正常追加不写控制头。具体耗时取决于芯片、Arduino Core、LittleFS分区和段大小；不得从ISR或timer callback调用，也不适合阻塞实时控制任务。通常把已完成的浇水、开关门、喂食记录投递到负责串行存储的任务；如果应用本身就在非实时loop中运行，也可以直接调用。不能为了缩短返回时间跳过flush，否则“返回成功”不再代表记录已经提交到Flash。

业务负责固定payload的编码、解码、字段版本、业务校验和显示。不得直接持久化含指针、`String`、编译器padding或平台相关布局的C++对象。六区域浇水详情应按区域1到区域6的固定位置连续编码；位置已经表达区域编号时不重复保存编号。

详细磁盘格式、容量规划、失败矩阵和实机性能数据见 [Record Store 设计、接入与实机基准](12_record_store.md)。

### 3.6 Esp32BaseAppEvents

仅在 `ESP32BASE_ENABLE_APP_EVENTS=1` 时可用；它自动启用RecordStore，并由 `Esp32Base::begin()` 在业务Store之前创建。条件跟踪宏 `ESP32BASE_ENABLE_APP_EVENT_CONDITIONS` 默认跟随App Events开启；显式设为0时仍保留离散事件记录，但不编译条件状态机、专属NVS访问和Web条件状态。默认预算：

```cpp
#define ESP32BASE_APP_EVENT_STORE_MAX_BYTES (100UL * 1024UL)
```

App Events不提供单独最低剩余空间宏。业务应在App Events创建后，根据LittleFS剩余空间规划自己的Store。v2固定24字节事件payload：

```text
offset  size  field
0       4     eventCode
4       4     reasonCode
8       4     objectId
12      4     value1
16      4     value2
20      1     flags
21      1     level
22      1     eventKind
23      1     conditionId
```

调用方输入不包含由基础库填写的 `eventKind/conditionId`：

```cpp
struct EventInput {
    uint32_t eventCode;   // 0 无效
    uint32_t reasonCode;  // 0 表示无
    uint32_t objectId;    // 0 表示无
    int32_t value1;
    int32_t value2;
    uint8_t flags;        // 完全由业务定义，8位
    Level level;          // uint8_t: Info/Warning/Error
};
```

RecordStore的20字节时间/ID元数据位于payload前，CRC32位于payload后，每条App Event仍为48字节。100 KiB逻辑预算估算容量2113条；按约4 KiB完整段轮换时实际保留约2029～2113条，路径为：

```text
/esp32base/records/app-events.v2/
```

v1与v2字段语义不同，不迁移、不兼容读取，也不自动删除v1目录；当前试用阶段的升级流程应在安装新固件前清空旧App Events，或由用户明确格式化LittleFS。

主要API：

```cpp
begin();
reload();
appendDiscreteEvent(const EventInput&);
observeConditionState(ConditionStateTracker&, ObservedConditionState, const EventInput&);
forgetConditionState(conditionId);
forgetAllConditionStates();
readLatest(offset, limit, callback, user);
readById(recordId, EventRecord&);
readStatus(AppEventsStatus&);
clearEventHistory();
```

`eventCode`、`reasonCode`、`objectId`、`value1/value2`和`flags`的业务含义完全由应用定义；基础库不解释RTC、缺料、门锁或通信语义，不保存显示文本，也不建立代码文字注册中心。所有App Event的RecordStore公共 `durationSec=0`；条件确认时间不是该字段的含义。

离散事件必须调用 `appendDiscreteEvent()`。每次调用都表示一次独立事实，即使所有字段与上一条完全相同也逐条保存。返回 `DiscreteEventAppendResult`，明确区分 `Stored`、`InvalidEvent`、`EventStoreUnavailable` 与 `EventStoreWriteFailed`；删除旧 `append()`，避免名称掩盖离散语义。

周期检测的持续状态使用一个由应用长期持有的tracker：

```cpp
Esp32BaseAppEvents::ConditionStateTracker rtcUnavailableCondition(
    1,      // conditionId: 1..32，在保留该NVS状态的固件版本间唯一且稳定
    5000,   // activationConfirmationMs
    10000   // recoveryConfirmationMs
);

Esp32BaseAppEvents::EventInput transitionEvent;
transitionEvent.eventCode = rtcMissing ? 3001 : 3002;
transitionEvent.level = rtcMissing ? Esp32BaseAppEvents::Level::Warning
                                   : Esp32BaseAppEvents::Level::Info;

const auto result = Esp32BaseAppEvents::observeConditionState(
    rtcUnavailableCondition,
    rtcMissing ? Esp32BaseAppEvents::ObservedConditionState::Active
               : Esp32BaseAppEvents::ObservedConditionState::Inactive,
    transitionEvent);
```

`ConditionStateTracker`只保存当前确认过程，不执行轮询、不持有硬件对象、不创建任务；它不可复制，必须是全局、静态或由应用长期持有且覆盖全部观察调用的成员对象。应用仍按自己的周期访问RTC或传感器。第一次观察到相反状态时开始确认，应用后续调用继续观察到同一状态且达到确认时间，才在该次调用中提交转换。确认时间为0表示立即确认，最大为 `INT32_MAX` 毫秒。计时使用无符号 `millis()` 差值，秒/分钟级确认可正常跨越约49.7天回绕；条件确认后不再计时，持续故障超过49天不会失效。`Unknown`取消尚未完成的确认，但保留已经确认的活动状态且不写存储。

条件ID固定为1～32；0保留给离散事件。它不是临时数组下标，而是 `active_id_bits` 的持久化schema：只要升级时保留 `eb_app_events` NVS，同一ID就必须继续表示同一个条件。计划删除或改变条件映射时，应用应在受控升级流程中先调用 `forgetConditionState()`/`forgetAllConditionStates()`或执行明确的基础库配置重置，不能直接复用仍可能活动的旧ID。基础库没有固定10槽数组：每个实际条件由应用持有一个tracker，库内只用32位活动ID位图和32位已登记ID位图。首次使用tracker时登记ID，重复ID返回 `InvalidArgument`。`EventKind`固定为 `Discrete`、`ConditionActivated`、`ConditionRecovered`，读取记录时同时返回 `conditionId`；激活和恢复可以使用相同或不同业务eventCode。

`ConditionObservationResult`明确区分：状态不变、激活/恢复确认中、激活/恢复事件已保存、未知观察、参数错误、Event Store不可用/写失败、条件NVS状态不可用、事件已保存但条件状态保存失败，以及因待保存状态而阻止转换。调用方不得把确认中、状态不变或部分失败伪装成“事件写入成功”。

确认转换时先同步提交一条LittleFS App Event，再更新RAM位图并把全部活动条件写入NVS的单个 `uint32_t`：

```text
eb_app_events.active_id_bits
```

状态不变、`Unknown`和确认等待均不写LittleFS或NVS。App Event写失败时不改变条件状态，RecordStore沿用 `WriteFault` 锁定，后续观察不继续尝试Flash写入；显式 `reload()` 后才恢复。事件成功但NVS失败时，返回 `EventStoredButConditionStateSaveFailed`，RAM保留新状态并阻止其他条件转换；`reload()`优先重试该位图。若在事件提交成功、NVS提交前掉电，重启后最多可能额外记录一次转换；为保持轻量不增加事务日志。

启动时NVS key不存在表示全部条件未确认；NVS读取错误不能按不存在处理，条件观察返回不可用，但离散事件仍可使用。重启后已活动条件再次观察为Active不会重复写；观察为Inactive则经过恢复确认。`forgetConditionState()`和`forgetAllConditionStates()`行政性忘记状态，不生成恢复事件；成功后会使当前进程内所有tracker的登记和未完成确认失效，各长期tracker在下一次观察时自动重新登记。忘记一个本来未活动的ID不写NVS，但同样允许替换该ID原先登记的tracker。`clearEventHistory()`只清LittleFS历史并保留条件NVS；LittleFS格式化后`reload()`重建v2 Store并重新读取NVS。启用条件跟踪时，`factoryReset()`/`clearLibraryNamespaces()`清除 `eb_app_events`，但不反向修改Runtime内存；正常情况下`reload()`重新读取已清空的NVS，若此前正有 `conditionStateSavePending`，`reload()`会按失败恢复契约优先重试RAM待保存状态。因此出厂重置完成后应重启设备，不能在待保存状态下用`reload()`代替重启。

System Logs、App Events和完整业务记录的边界：

| 能力 | 记录内容 |
| --- | --- |
| `Esp32BaseFileLog` | boot、WiFi、OTA、LittleFS等技术诊断文本 |
| `Esp32BaseAppEvents` | 开门受阻、喂食因缺料跳过、浇水因缺水停止等低频业务决策和影响 |
| `Esp32BaseRecordStore` | 完整开关门、喂食、浇水历史事实 |

内置HTML、JSON和CSV只输出CRC有效事件，支持最新优先分页以及level、真实/相对时间、eventCode和reasonCode筛选。内置HTML列表按行提供详情弹层，分组展示该事件的全部公开通用时间元数据和数值字段；页面不替业务映射中文说明，也不暴露CRC、slot等内部存储布局。损坏记录只在Store状态中计数，不作为事件返回。

示例：

```cpp
Esp32BaseAppEvents::EventInput event;
event.level = Esp32BaseAppEvents::Level::Warning;
event.eventCode = 1002;   // 业务定义：开门受阻
event.reasonCode = 2101;  // 业务定义：电流过高
event.objectId = 1;       // 门1
event.value1 = 1450;      // 业务定义：峰值mA
event.value2 = 2;         // 业务定义：重试次数
event.flags = 0x01;       // 业务定义
const auto result = Esp32BaseAppEvents::appendDiscreteEvent(event);
```

## 4. Esp32BaseConfig

后端：ESP32 NVS。

限制：

- namespace 长度 `1..15`。
- key 长度 `1..15`。
- string value 可见内容长度不超过 3999 字节。
- blob value 长度为 `1..256` 字节，用于小型固定大小 POD 元数据，不用于日志、记录正文或大块业务数据。
- 库内部 namespace 全部使用 `eb_` 前缀，例如 `eb_wifi`、`eb_sys`、`eb_log`、`eb_ui`、`eb_app_events`。
- namespace 已表达库和模块归属，key 不重复模块前缀，例如 `eb_wifi.ssid`、`eb_wifi.pass`、`eb_sys.rst_cnt`、`eb_sys.wdt_cnt`、`eb_sys.wdt_trip_base`、`eb_sys.wdt_trip_time`、`eb_log.mode`、`eb_web.auth_user`、`eb_web.auth_pass`、`eb_ui.footer_mode`、`eb_app_events.active_id_bits`。
- 应用不得使用 `eb_` 前缀，避免被库维护 API 清理。
- `Esp32BaseConfig` 不是跨任务线程安全 API；推荐和 `Esp32Base::begin()` / `handle()`、Web、Bus 固定在同一个 loop/system task 中调用，其他任务通过队列投递配置变更。
- 单个 NVS key 写入依赖 NVS 自身的断电保护；多个 key 组成的业务动作不是事务。App Config 多字段提交会逐字段写入，某字段失败后停止继续写后续字段并返回 partial；需要强一致的一组业务配置应合并为单个 POD/blob 或使用业务自定义提交模型。

建议 API：

```cpp
class Esp32BaseConfig {
public:
    static bool begin();
    static void handle();
    static bool isReady();

    static bool setStr(const char* ns, const char* key, const char* value);
    static bool getStr(const char* ns, const char* key, char* out, size_t len, const char* def = "");

    static bool setInt(const char* ns, const char* key, int32_t value);
    static int32_t getInt(const char* ns, const char* key, int32_t def = 0);
    static bool setIntDeferred(const char* ns, const char* key, int32_t value, uint32_t delayMs = 1000);

    static bool setBool(const char* ns, const char* key, bool value);
    static bool getBool(const char* ns, const char* key, bool def = false);
    static bool setBoolDeferred(const char* ns, const char* key, bool value, uint32_t delayMs = 1000);

    static bool setStrDeferred(const char* ns, const char* key, const char* value, uint32_t delayMs = 1000);

    static bool setBlob(const char* ns, const char* key, const void* data, size_t len);
    static bool getBlob(const char* ns, const char* key, void* out, size_t len);
    static bool setBlobDeferred(const char* ns, const char* key, const void* data, size_t len, uint32_t delayMs = 1000);

    template <typename T>
    static bool setPod(const char* ns, const char* key, const T& value);

    template <typename T>
    static bool getPod(const char* ns, const char* key, T& out, const T& def = T());

    template <typename T>
    static bool setPodDeferred(const char* ns, const char* key, const T& value, uint32_t delayMs = 1000);

    static bool flushNextDue();
    static bool flushNextForced();
    static bool flushAll();

    static uint8_t pendingCount();
    static uint8_t pendingCapacity();

    static bool clearNamespace(const char* ns);
    static bool clearWifiConfig();
    static bool clearWebAuthConfig();
    static bool clearSystemConfig();
    static bool clearLogConfig();
    static bool clearUiConfig();
    static bool factoryReset();
    static bool clearLibraryNamespaces();

    static void pauseDeferredFlush();
    static void resumeDeferredFlush();
    static bool isDeferredFlushPaused();

    static void enableConfigAudit(bool enabled);
    static void enableConfigReadAudit(bool enabled);
    static bool isConfigAuditEnabled();
    static bool isConfigReadAuditEnabled();
};
```

OTA 上传期间 deferred flush 暂停。

deferred 语义：

- `setXxxDeferred()` 写入 pending 队列后立即对读取可见。
- `getXxx()` 必须先查 pending 队列，未命中再读 NVS，因此 set 后立即 get 总是返回最新值。
- `getStr()` 找不到 key 时返回 false，并把默认值写入输出缓冲；`getInt()` / `getBool()` 找不到 key 时返回默认值。
- NVS 读取必须先判断 key 是否存在，避免 Arduino `Preferences` 在缺 key 时输出底层 `NOT_FOUND` 噪音。
- 字符串读取和写前比较使用固定 scratch buffer，不使用 Arduino `String` 拼接或读取，减少配置高频读取造成的 heap 碎片风险。
- 同一 namespace/key 的 pending 写入会合并为最新值，不重复占用多条 pending。
- `setXxxDeferred()` 如果 pending 中已有相同值，会返回 true 并保留原 pending/due 时间；如果 NVS 旧值已经相同，会返回 true、清除同 key pending 并跳过新的 NVS 写入。
- deferred 到期判断使用 `millis()` 差值比较，覆盖正常延迟窗口内的 49 天回绕；不要把 deferred delay 设置为接近或超过 `INT32_MAX` ms 的长期定时任务。
- OTA 上传期间只暂停 deferred flush；`getXxx()`、`pendingCount()`、`flushAll()` 和 `clearLibraryNamespaces()` 仍按各自语义工作。
- `factoryReset()` 只清理当前已启用的基础库 NVS 配置（启用条件跟踪时包括 `eb_app_events`），不重启、不格式化 LittleFS、不删除App Event/FileLog内容、不清理业务 namespace，不清理 boot/restart/watchdog 统计诊断资产。namespace不存在按已清理处理；NVS namespace打开或清除失败必须返回false，不能伪装成不存在。它不直接修改App Events运行态缓存，出厂重置流程应在成功后重启；只有确认不存在条件状态待保存时，才可用显式 `Esp32BaseAppEvents::reload()` 重新读取已清理NVS。
- `clearWifiConfig()` 清理 `eb_wifi`，包含 WiFi SSID/password。
- `clearWebAuthConfig()` 清理 `eb_web`，包含 Web Auth user/password。
- `clearSystemConfig()` 只清理 `eb_sys.hostname`；`eb_sys.rst_cnt`、restart log、`boot_cnt`、`wdt_cnt`、`wdt_trip_base`、`wdt_trip_time` 等统计/诊断 key 必须保留。
- `clearLogConfig()` 清理 `eb_log`，包含 FileLog 配置。
- `clearUiConfig()` 清理 `eb_ui`，包含 Footer bar 显示模式。
- 启用条件跟踪时，`factoryReset()` 和 `clearLibraryNamespaces()` 清理内部 `eb_app_events.active_id_bits`；能力关闭时不引用该namespace，也不提供业务直接写该key的公开API。
- `clearLibraryNamespaces()` 是库级配置清理入口，当前语义等价于 `factoryReset()`：只清理基础库 NVS 配置，保留统计/诊断资产、业务 namespace 和 LittleFS 内容。
- `clearNamespace()` 和各出厂重置 API 在 namespace 不存在时返回成功，不创建空 namespace，也不输出底层 `NOT_FOUND` 噪声。

基础库出厂重置推荐流程：

```cpp
Esp32BaseConfig::factoryReset();
Esp32BaseConfig::flushAll();
Esp32BaseSystem::restart("factory reset");
```

完整整机出厂重置应由业务项目先清理自己的 namespace，再调用 `factoryReset()`。

配置审计可以在 `Esp32Base::begin()` 前调用。如果需要覆盖基础库初始化过程中的配置读取/写入，必须在 begin 前开启；如果只关心业务运行期配置变化，也可以在 begin 后开启。

```cpp
void setup() {
    Esp32BaseLog::setSerialLevel(Esp32BaseLog::INFO);
    Esp32BaseConfig::enableConfigAudit(true);
    Esp32BaseConfig::enableConfigReadAudit(true);

    Esp32Base::begin();
}
```

写入/flush 成功多为 INFO；读取审计、未变化跳过和 deferred 入队多为 DEBUG。需要读取审计时，编译期 `ESP32BASE_LOG_LEVEL` 必须至少为 `ESP32BASE_LOG_DEBUG`。

blob / POD 语义：

- `setBlob()` / `setBlobDeferred()` 的 `data` 不能为 `nullptr`，`len` 必须在 `1..256`；非法 namespace、key、data 或 len 返回 false。
- `getBlob()` 要求调用方传入准确的固定大小；NVS 中不存在 key、读取失败或存储长度与 `len` 不一致时返回 false。
- `setBlob()` 写入前会读取旧值；旧值长度和内容完全相同时返回 true 并跳过 NVS 写入，同时清除同 key pending。
- `setBlobDeferred()` 复用统一 deferred 去重语义：pending 或 NVS 旧值已经相同会返回 true 并跳过新的 NVS 写入；否则入队并由 `handle()` / `flushAll()` 落盘。
- `getBlob()` 和 `getPod()` 与其他 `getXxx()` 一样先读 pending，同 key deferred 写入后立即可见。
- `setPod()` / `getPod()` / `setPodDeferred()` 是 blob API 的模板薄封装，只接受 trivially copyable 且 standard-layout 的固定布局结构；不要保存含指针、虚函数、`String`、容器或跨固件版本布局不稳定的类型。
- 如果 POD 结构未来需要改变布局，业务应自己在结构中放 `version` 字段并按应用策略迁移；Config 不替业务解释 blob 内容。

POD 元数据示例：

```cpp
struct StoreMeta {
    uint32_t head;
    uint32_t count;
    uint32_t nextId;
};

StoreMeta meta = {};
Esp32BaseConfig::getPod("app_store", "meta", meta);

meta.head = nextHead;
meta.count = nextCount;
meta.nextId = nextId;

if (!Esp32BaseConfig::setPodDeferred("app_store", "meta", meta, 1000)) {
    ESP32BASE_LOG_W("app", "store meta enqueue failed");
}

// restart、deep sleep、OTA success 等关键路径前仍调用 flushAll()。
Esp32BaseConfig::flushAll();
```

多任务用法：

- Config API 按单任务模型设计，只能在 Arduino `loopTask` 或同一个系统服务任务中统一调用。
- `Esp32Base::begin()`、`Esp32Base::handle()`、`Esp32BaseConfig`、`Esp32BaseWeb` 和 `Esp32BaseBus` 推荐固定在 Arduino `loopTask`，或固定在应用自建的同一个系统服务任务中调用。
- `Esp32BaseConfig` 当前不是线程安全 API；业务 FreeRTOS task 不应从另一个核或另一个 task 直接调用 `setXxx()`、`setXxxDeferred()`、`flushAll()` 或 `clearNamespace()`。
- 业务 task 需要改配置时，应通过 FreeRTOS queue、flag 或 ring buffer 把请求投递给 loop/system task，再由这个任务统一调用 Config API。
- 实时任务不要直接做 NVS、LittleFS、Web、OTA、FileLog 操作；这些操作可能写 flash、分配资源或阻塞网络/文件系统，容易影响实时响应。

典型做法是业务 task 只投递轻量消息，loop/system task 在调用 `Esp32Base::handle()` 的同一上下文中消费消息并调用 Config API。队列、ring buffer 或应用自定义 mailbox 均可，关键是不要跨 task 直接写 NVS/FS 或调用 Web/OTA 维护路径。

## 5. Esp32BaseSystem

`Esp32BaseSystem` 不公开 OTA mark / rollback API。OTA 语义通过 `Esp32BaseOta` 暴露。

建议 API：

```cpp
class Esp32BaseSystem {
public:
    static bool begin();
    static bool isReady();

    static uint32_t freeHeap();
    static uint32_t minFreeHeap();
    static uint32_t totalHeap();
    static uint32_t flashSize();
    static uint32_t uptimeMs();
    static uint64_t uptimeMs64();
    static uint32_t bootCount();

    static const char* resetReason();
    static const char* resetReasonText();
    static const char* wakeReason();
    static const char* wakeReasonText();

    static void restart(const char* reason);

    static bool appendRestartLog(const char* reason);
    static uint8_t restartLogCount();
};
```

`uptimeMs()` 保留 Arduino `millis()` 的 32 位语义，约 49.7 天回绕；长期运行诊断应使用 `uptimeMs64()`，它基于 ESP 定时器返回 64 位毫秒值。内置 Web Status 和 footer 使用 64 位接口，因此不会在 49.7 天后把在线时长归零。

`bootCount()` 返回无符号重启计数，存储在 `eb_sys.boot_cnt`，使用 `uint32_t` 语义在上电、手动复位、`ESP.restart()`、Watchdog、OTA 重启等会清除普通内存的非 sleep 重启后加 1。deep sleep 唤醒不会增加该计数，也不会为 `boot_cnt` 写 NVS；具体启动原因通过 `resetReason()` / `wakeReason()` 判断。

`resetReason()` 表示芯片本次为什么复位或重新启动，例如 `poweron`、`software`、`task_wdt`、`deep_sleep`。`resetReasonText()` 返回对应中文说明，例如 `上电启动`、`软件重启`、`看门狗复位`。

`wakeReason()` 只表示从 sleep 唤醒的来源，例如 `timer`、`ext0`、`gpio`；普通上电或普通复位没有 sleep 唤醒来源，因此返回 `undefined` 是正常状态。`wakeReasonText()` 返回对应中文说明，其中 `undefined` 对应 `非睡眠唤醒`。

`restartLogCount()` 返回库维护的重启/休眠生命周期日志计数，存储在 `eb_sys.rst_cnt`，最大饱和为 255。库内部保留最近 4 条 reason 作为诊断环。`Esp32BaseSystem::restart()`、`Esp32BaseSleep::deepSleep*()` 和 OTA rollback 进入重启/休眠前会写入该 lifecycle reason；秒级 deep sleep 设备如果长期调用库 sleep API，会产生对应 NVS 写入。是否跳过 deep sleep lifecycle log 属于诊断语义变化，不能为了 Flash 寿命默认改变。

## 6. Esp32BaseBus

仅在启用 Bus 时可用。

要求：

- `publish()` 只能在 Arduino loop 任务调用。
- `begin()` 记录 loop task handle。
- `publish()` 检查当前 task，不匹配则返回 false；开启 `ESP32BASE_BUS_STRICT_TASK_CHECK=1` 时 assert。
- `publish()` 先快照本次匹配的订阅者，再调用 callback；callback 中允许 `unsubscribe()`，已取消的后续订阅不会在本次 publish 中继续收到事件，新订阅不会收到当前正在发布的事件。

事件常量规则：

- 所有会 publish 事件的公开模块，必须在对应 class 内导出 `static constexpr const char* EVENT_*`。
- 事件名使用小写点分格式，例如 `wifi.connected`、`ota.progress`。
- 事件名最大可见长度为 31 字节；`subscribe()` 对更长事件名返回 0，避免静默截断后出现不可诊断的匹配问题。
- WiFi callback、timer callback、ISR 或 LWIP 回调不得直接 publish，必须入队后在 `Esp32Base::handle()` 中 publish。

建议 API：

```cpp
class Esp32BaseBus {
public:
    using Callback = void (*)(const char* event, const char* data, void* user);
    using SubscriptionId = uint16_t;

    static bool begin();
    static SubscriptionId subscribe(const char* event, Callback cb, void* user = nullptr);
    static bool publish(const char* event, const char* data = "");
    static bool unsubscribe(SubscriptionId id);
    static uint16_t subscriberCount();
    static uint16_t subscriberCapacity();
};
```

## 7. Esp32BaseWiFi

建议 API：

```cpp
class Esp32BaseWiFi {
public:
    struct RecoveryButtonConfig {
        bool enabled;
        int8_t gpio;
        uint32_t holdMs;
    };

    enum State : uint8_t {
        IDLE,
        CONNECTING,
        CONNECTED,
        CONFIG_PORTAL,
        RETRY_BACKOFF,
        FAILED
    };

    static constexpr const char* EVENT_CONNECTED = "wifi.connected";
    static constexpr const char* EVENT_DISCONNECTED = "wifi.disconnected";
    static constexpr const char* EVENT_CONFIG_PORTAL = "wifi.config_portal";
    static constexpr const char* EVENT_RETRY = "wifi.retry";
    static constexpr const char* EVENT_FAILED = "wifi.failed";

    static bool begin();
    static void handle();
    static bool isReady();

    static bool connect(const char* ssid, const char* password, bool persist = true);
    static bool clearCredentials();
    static bool retrySavedCredentials();
    static bool startConfigPortal();
    static bool stopConfigPortal();
    static RecoveryButtonConfig recoveryButtonConfig();
    static RecoveryButtonConfig defaultRecoveryButtonConfig();
    static bool setRecoveryButtonConfig(const RecoveryButtonConfig&);
    static bool isValidRecoveryButtonConfig(const RecoveryButtonConfig&);
    static bool recoveryButtonTriggered();

    static State state();
    static const char* stateName();
    static bool isConnected();
    static const char* ssid();
    static bool safeBootPaused();
    static uint8_t retryCount();
    static uint32_t retryRemainingMs();
    static uint8_t safeBootGuardedResetCount();
    static bool ip(char* out, size_t len);
    static int32_t rssi();

    static void setPowerSave(bool enabled);
    static bool powerSave();
};
```

WiFi 凭证和重连策略：

- `retryCount()` 返回当前连续重连周期已经执行的次数；连接成功、显式重新连接、清除凭证或进入配置热点时归零。`retryRemainingMs()` 只在 `RETRY_BACKOFF` 状态返回距离下一次尝试的剩余毫秒数，其他状态返回 `0`。两者均为已有状态的只读快照，不触发扫描或连接。
- 无已保存凭证时，`begin()` 可进入 `CONFIG_PORTAL`。
- 默认 config portal AP 不设置密码；SSID 为 `ESP32-Config-XXXX`，其中 `XXXX` 取 eFuse MAC 按常见网络 MAC 顺序显示时的最后两个字节。
- WiFi恢复按键默认启用：ESP32 / ESP32-S3默认GPIO0，ESP32-C3默认GPIO9，使用内部上拉、低电平有效。应用运行后连续稳定按下达到默认10000ms时立即调用`startConfigPortal()`，无需先松手；同一次按下只触发一次。该动作不清除`eb_wifi`凭据、不重启，避免默认BOOT引脚仍为低电平时进入下载模式；如果按键在复位或上电期间已经按下，芯片仍会先按硬件strapping规则进入ROM下载模式，应用无法接管。
- 恢复按键能力可由`ESP32BASE_ENABLE_WIFI_RECOVERY_BUTTON=0`完整裁掉；编译能力存在时，构建默认可由`ESP32BASE_WIFI_RECOVERY_BUTTON_DEFAULT_ENABLED`、`ESP32BASE_WIFI_RECOVERY_BUTTON_GPIO`和`ESP32BASE_WIFI_RECOVERY_BUTTON_HOLD_MS`覆盖。运行时配置使用版本化7字节线性编码保存在`eb_wifi_rcv.button`，System页或`setRecoveryButtonConfig()`保存后立即重新配置GPIO；允许的长按范围为1000～60000ms。
- `isValidRecoveryButtonConfig()`要求GPIO是Arduino ESP32 Core报告的有效、可启用内部上拉的GPIO，并拒绝当前芯片已知的片内Flash GPIO范围。它无法识别具体业务板上的外设、PSRAM、USB或应用引脚冲突，调用方仍必须核对原理图。关闭功能也必须提交合法GPIO，以保证以后重新启用时配置完整。
- `recoveryButtonTriggered()`表示本次启动中是否至少成功达到过一次长按阈值；它不表示热点当前仍在运行。保存新WiFi会沿用既有“Save & Connect”行为，立即停止热点并尝试新网络；未保存时，重启会再次按保留的旧凭据启动。
- 有效凭证要求 SSID 非空且不超过 32 字节，密码可为空且不超过 64 字节；超限输入返回 false，不静默截断。
- 有已保存凭证但连接失败时，不自动进入 AP/config portal，而是持续重连。
- 单次 STA 连接尝试有非阻塞超时，默认 `ESP32BASE_WIFI_CONNECT_TIMEOUT_MS=15000`。
- `WL_IDLE_STATUS` 表示 STA 已关联 AP、正在等待 IPv4 地址时，不按普通连接超时反复调用 `WiFi.begin()`；基础库保留关联并等待 DHCP，默认 `ESP32BASE_WIFI_DHCP_TIMEOUT_MS=120000`。这适用于 DHCP 位于上级路由、Mesh 或中继上联可能短时不可用的网络。超时后才进入既有 backoff，backoff 期间若异步获得地址仍立即进入 `CONNECTED`。
- 已处于 `CONNECTED` 时如果丢失 IPv4 但 STA 仍关联，发布一次 `wifi.disconnected` 并进入 DHCP 等待；只有关联也丢失或 DHCP 等待超时后才按普通重连策略处理。
- `CONNECTED` 只表示 STA 已获得 IP，不表示互联网可用；NTP 或公网访问失败不触发 WiFi 断开或重连。
- 前几次重连使用短间隔，连续失败后进入长间隔 backoff；backoff 必须非阻塞，不影响 `handle()`、Watchdog feed 和必要休眠。
- 初期连接失败和短间隔重试保留 WARN，便于现场排查切到 WARN 时捕获首次故障；进入慢速 backoff 后，重复的连接超时和重试排程日志降为 INFO，避免长期离线设备在默认 ERROR FileLog 下持续写 Flash。
- WiFi 初始化安全启动保护：`begin()` 在任何 Arduino `WiFi.*` 初始化/模式调用前写入 `eb_wifi.init_guard`。如果下一次启动发现该标记仍存在，且 reset reason 是 `brownout`、`panic`、watchdog 类复位或 WiFi 初始化期常见的 `software` 复位，则累计 `eb_wifi.init_rst`；连续达到 `ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS` 后设置 `eb_wifi.init_pause=true`，本轮跳过 `WiFi.persistent()`、`WiFi.setSleep()`、`WiFi.mode()`、`WiFi.begin()` 和 AP 初始化，让系统至少进入无 WiFi 诊断状态。
- `init_pause=true` 时，如果后续启动 reset reason 仍属危险复位，`begin()` 继续跳过 WiFi，并输出中文醒目提醒：疑似 WiFi/RF 启动瞬时电流导致供电跌落，应检查电源、USB 线、稳压器余量和接线，并考虑在板端 VIN/5V 与 GND 间增加低 ESR 储能电容。该日志是供电风险诊断建议，不把电容不足判定为唯一原因，也不会关闭 brownout detector。
- STA 安全启动保护：有已保存凭证并准备进入 STA 时，库会在 `eb_wifi.sta_guard` 写入 guarded 启动标记；成功连接后清除 `sta_guard`、`sta_rst` 和 `sta_pause`。如果下一次启动发现 guarded 标记仍存在，且本次 reset reason 是 `brownout`、`panic` 或 watchdog 类复位，则累计 `eb_wifi.sta_rst`；连续达到 `ESP32BASE_WIFI_SAFE_BOOT_MAX_RESETS`（默认 3）后设置 `eb_wifi.sta_pause=true`，保留 `ssid/pass`，暂停 STA 并进入 AP/config portal。
- `sta_pause=true` 时，如果本次 reset reason 仍是 brownout、panic 或 watchdog 类危险复位，`begin()` 会把保存凭据视为暂不可用并进入 `CONFIG_PORTAL`；如果本次是 `poweron`、外部复位或其他非危险复位，库会自动清除 pause/guard/count 并用已保存凭据恢复一次 STA 尝试。
- `retrySavedCredentials()` 会重新读取已保存凭据，清除 safe boot pause/guard/count，停止 config portal，并用原凭据进入 STA 连接流程；它不要求业务或用户重新保存同一组 SSID/password。WiFi 页面在 safe boot paused 时提供“恢复并重试已保存 WiFi”入口调用该行为。
- 只有无凭据、STA安全启动保护、恢复按键、显式`startConfigPortal()`或应用自定义策略，才能进入AP/config portal。
- `clearCredentials()` 只清空库管理的 WiFi 凭证，不立即触发重连、断线或 portal；NVS 清理失败时返回 false。
- `connect(..., persist=true)` 必须先同步写入 NVS 保存凭证，写入失败时返回 false 且不切换连接；这是显式配置提交的可靠性取舍，避免页面提交后立即重启导致新凭证丢失。
- WiFi 日志可输出 SSID 和 `password_set` 状态，不输出密码值。
- `begin()` 默认禁用 WiFi modem sleep，并在启动时显式下发 `WiFi.setSleep(false)`，使 Web/OTA 请求不受 Arduino ESP32 默认 `WIFI_PS_MIN_MODEM` 的 DTIM 唤醒延迟影响；电池设备可调用 `setPowerSave(true)` 恢复 modem sleep，但需要接受 Web 首屏可见延迟。
- 进入 deep sleep 后 STA、AP、DNS、Web 均不可用；唤醒相当于新一轮启动，按凭证状态恢复网络。
- 默认无限重试；`FAILED` 状态通常只在 STA mode 切换失败等底层异常下出现，安全启动触发时会发布 `wifi.failed` 并回退 AP/config portal。
- 仅当应用显式设置有限 maxRetries 且全部用尽，才进入 `FAILED`。
- 从 `FAILED` 恢复必须通过 `connect()` 重新提交凭证，或 `clearCredentials()` 后再显式 `startConfigPortal()`。

## 8. Esp32BaseTime / Rtc / Dns / Ntp / Mdns

```cpp
class Esp32BaseTime {
public:
    enum Source : uint8_t {
        SOURCE_UPTIME = 0,
        SOURCE_RTC = 1,
        SOURCE_NTP = 2
    };

    struct Snapshot {
        bool synced;
        Source source;
        uint32_t epochSec;
        uint32_t uptimeSec;
        uint32_t bootId;
        uint32_t bootStartEpochSec;
    };

    typedef void (*TimeSyncCallback)(const Snapshot& snapshot);

    static bool initBootSession();
    static Snapshot snapshot();
    static bool isRealTime();
    static bool formatEpoch(uint32_t epochSec, char* out, size_t len, const char* fmt = nullptr);
    static bool formatTime(char* out, size_t len, const char* fmt = nullptr);
    static bool resolveCurrentBootEvent(uint32_t bootId, uint32_t uptimeSec, uint32_t* epochSec);
    static void onTimeSynced(TimeSyncCallback callback);
    static const char* sourceName(Source source);
    static const char* logTimeString();
};

class Esp32BaseRtc {
public:
    enum Driver : uint8_t {
        DRIVER_DS3231 = 1,
        DRIVER_PCF8563 = 2
    };

    enum Status : uint8_t {
        STATUS_DISABLED = 0,
        STATUS_NOT_STARTED = 1,
        STATUS_OK = 2,
        STATUS_MISSING = 3,
        STATUS_I2C_ERROR = 4,
        STATUS_TIME_INVALID = 5,
        STATUS_CLOCK_STOPPED = 6,
        STATUS_CONFIG_ERROR = 7
    };

    static bool configure(TwoWire& wire, uint8_t address = 0);
    static bool begin();
    static void handle();
    static bool refresh();
    static bool isAvailable();
    static bool isTimeValid();
    static bool readEpoch(uint32_t* epochSec);
    static bool setEpoch(uint32_t epochSec);
    static Status status();
    static const char* statusText();
    static const char* driverName();
    static uint32_t lastEpoch();
    static uint32_t lastSyncUptimeSec();
};

class Esp32BaseDns {
public:
    static bool begin();
    static void handle();
    static void stop();
    static bool isRunning();
};

class Esp32BaseNtp {
public:
    struct TimeSnapshot {
        bool synced;
        uint32_t epochSec;
        uint32_t uptimeSec;
        uint32_t bootId;
        uint32_t bootStartEpochSec;
    };

    typedef void (*TimeSyncCallback)(const TimeSnapshot& snapshot);

    static bool initBootSession();
    static bool begin();
    static bool isStarted();
    static bool isTimeSynced();
    static bool isRealTime();
    static uint32_t timestamp();
    static TimeSnapshot snapshot();
    static void onTimeSynced(TimeSyncCallback callback);
    static bool isCurrentBootEvent(uint32_t bootId);
    static bool canResolveCurrentBootEvent(uint32_t bootId);
    static bool resolveCurrentBootEvent(uint32_t bootId, uint32_t uptimeSec, uint32_t* epochSec);
    static void setServers(const char* s1, const char* s2 = nullptr, const char* s3 = nullptr);
    static bool formatTime(char* out, size_t len, const char* fmt);
};

class Esp32BaseMdns {
public:
    static bool begin();
    static void stop();
    static bool isRunning();
    static bool addHttpService(uint16_t port = 80);
};
```

`Esp32BaseDns` 在 config portal 模式下对所有 DNS 查询返回 AP IP，不维护固定域名白名单，也不提供 `addCaptiveTarget()`。

`Esp32BaseTime` 是业务侧统一时间入口。业务记录、时间显示和业务页面应优先使用 `Esp32BaseTime::snapshot()`、`formatTime()` 和 `resolveCurrentBootEvent()`；不要把业务逻辑直接绑定到 NTP。`source` 表示当前可信时间来源：未同步时为 `SOURCE_UPTIME`，RTC 成功读取后为 `SOURCE_RTC`，NTP 成功同步后升级为 `SOURCE_NTP`。NTP 比 RTC 优先级高，后续 RTC refresh 不会把来源从 NTP 降级。

`formatEpoch()` / `formatTime()` 使用 `ESP32BASE_NTP_GMT_OFFSET_SEC` 和 `ESP32BASE_NTP_DAYLIGHT_OFFSET_SEC` 做固定偏移格式化，默认 UTC+8。该格式化不依赖 SNTP 是否已经启动，因此 RTC-only 或 WiFi 尚未连接时也能按同一规则显示真实时间。

`ESP32BASE_TIME_SYNC_MIN_EPOCH` 是统一可信 epoch 下限，默认跟随 `ESP32BASE_NTP_SYNC_MIN_EPOCH`，当前为 `1700000000UL`。低于该值的 RTC/NTP 时间会被拒绝，避免把未初始化或异常回退的时间当成真实时间。

`Esp32BaseRtc` 是 RTC 芯片维护接口，只在 `ESP32BASE_ENABLE_RTC=1` 时可用。当前支持 `ESP32BASE_RTC_DRIVER_DS3231` 和 `ESP32BASE_RTC_DRIVER_PCF8563`，同一个固件只能选择一个驱动，不做自动识别。RTC 芯片寄存器按 UTC 日历字段存储；NTP 写回 RTC 时也写 UTC 字段，避免本地时区和 DST 歧义。默认地址：

- DS3231: `0x68`
- PCF8563: `0x51`

RTC 配置项：

```ini
build_flags =
  -D ESP32BASE_ENABLE_RTC=1
  -D ESP32BASE_RTC_DRIVER=ESP32BASE_RTC_DRIVER_DS3231
  -D ESP32BASE_RTC_I2C_ADDR=0
  -D ESP32BASE_RTC_AUTO_WIRE_BEGIN=0
  -D ESP32BASE_RTC_NTP_WRITEBACK=1
```

`ESP32BASE_RTC_I2C_ADDR=0` 表示使用所选驱动默认地址。`ESP32BASE_RTC_AUTO_WIRE_BEGIN=0` 时，应用负责在 `Esp32Base::begin()` 前初始化 I2C 并可调用 `Esp32BaseRtc::configure(Wire)` 或绑定自定义 `TwoWire`；设置为 `1` 时，基础库会为 RTC 调用 `Wire.begin()`，可配合 `ESP32BASE_RTC_SDA`、`ESP32BASE_RTC_SCL`、`ESP32BASE_RTC_I2C_CLOCK_HZ` 使用。RTC 缺失、振荡停止或时间非法不会让 `Esp32Base::begin()` 失败；状态通过 `Esp32BaseRtc::status()`、Status 页和日志暴露。

基础库只使用 RTC 的时间读写能力。DS3231/PCF8563 的中断、闹钟、方波、温度等扩展能力不由基础库占用；业务项目可以继续通过 I2C 访问芯片扩展寄存器，但必须自行负责寄存器语义和并发时序。基础库不接管中断引脚。

启用 NTP 和 RTC 时，NTP 首次可信同步后会按 `ESP32BASE_RTC_NTP_WRITEBACK` 和 `ESP32BASE_RTC_WRITEBACK_THRESHOLD_SEC` 决定是否写回 RTC，默认开启，阈值为 2 秒。写回失败只记录状态，不影响 NTP 时间对业务生效。

NTP 默认使用 UTC+8，即 `ESP32BASE_NTP_GMT_OFFSET_SEC=(8L * 3600L)`、`ESP32BASE_NTP_DAYLIGHT_OFFSET_SEC=0L`；应用可在 include 前用宏覆盖。

`Esp32BaseNtp` 只表示 NTP 客户端状态。`isTimeSynced()` 默认要求 SNTP completed 且当前 epoch >= `ESP32BASE_NTP_SYNC_MIN_EPOCH`。本 boot 一旦完成可信 NTP 同步并建立 `bootStartEpochSec`，后续 `isTimeSynced()` / `snapshot().synced` 不再要求 `sntp_get_sync_status()` 持续保持 completed，只要系统 epoch 仍可信且统一时间来源仍为 NTP 就保持 true。RTC-only 设备应使用 `Esp32BaseTime`，而不是把 `Esp32BaseNtp` 当成通用真实时间 API。

离线业务事件应使用 `Esp32BaseTime::snapshot()` 获取统一时间快照：

- `synced=true` 表示当前时间已通过 Esp32Base 可信判定，和 `/esp32base/status` Status 页 Time 行使用同一语义。
- `epochSec` 仅在 `synced=true` 时有效；未同步时为 `0`，业务不应伪造日期。
- `uptimeSec` 和 `bootId` 在未同步时也可用，用于记录“本次开机 +N 秒”的业务事件；`uptimeSec` 来自 ESP-IDF 64-bit 运行时间计数源，不受 Arduino `millis()` 约 49.7 天回卷影响。
- `bootStartEpochSec` 仅在本次 boot 已同步后有效，值为 `epochSec - uptimeSec`。

`Esp32Base::begin()` 会在系统模块初始化后调用 `Esp32BaseTime::initBootSession()`，`bootId` 复用 `Esp32BaseSystem::bootCount()`，不为时间模块额外写启动期 NVS。`bootId=0` 保留为未知；正常非 sleep 重启使用非零 boot count，deep sleep 唤醒沿用上一次非 sleep 重启计数。业务项目不需要手动调用 `initBootSession()`，除非绕过 `Esp32Base::begin()` 直接使用时间模块。

`Esp32BaseTime::onTimeSynced(callback)` 注册一个轻量单回调；如果注册时当前 boot 已有可信真实时间，会立即回调一次。回调参数是同步瞬间的 `Snapshot`，业务可据此扫描自己的日志，把同一 `bootId` 的相对 `uptimeSec` 事件回填为真实时间。基础库不理解也不改写业务日志结构。`Esp32BaseNtp::onTimeSynced(callback)` 只在 NTP 同步语义下使用。

回填 helper 语义：

- `isCurrentBootEvent(bootId)` 只判断事件是否属于本次 boot。
- `canResolveCurrentBootEvent(bootId)` 要求事件属于本次 boot 且已有 `bootStartEpochSec`。
- `resolveCurrentBootEvent(bootId, uptimeSec, &epochSec)` 只转换本次 boot 的相对时间；历史 boot、未知 `bootId`、未同步状态都会返回 `false`，避免把历史未知时间误修正。

NTP 日志策略：

- `isTimeSynced()` 是状态查询，不代表发生了一次 NTP 同步尝试；未同步状态不输出周期性 WARN/DEBUG。
- `ntp_client_started` 和对时成功日志使用 INFO。
- 只有接入明确的单次同步失败事件时，才应为该失败事件输出 WARN。

NTP 对时成功日志应能帮助把启动后毫秒时间轴映射到真实时间，至少包含：

- 当前实际日期时间。
- 当前 uptime。
- 推算出的 boot wall time。

日志时间戳在没有可信真实时间前使用启动后的 `millis()`，例如 `[42442]`；RTC 或 NTP 建立可信时间后切换为绝对日期时间，例如 `[2026-05-05 12:45:55]`。NTP 对时成功时应输出可用于换算早期毫秒时间轴的诊断信息。

## 9. Esp32BaseRs485Port

仅在 `ESP32BASE_ENABLE_RS485_PORT=1` 时可用。该模块只封装 ESP32 `HardwareSerial` 半双工 RS485 的通用方向控制能力，不提供 Modbus/RTU、CRC、地址、超时帧、重试、寄存器或任何应用协议。

```cpp
class Esp32BaseRs485Port {
public:
    struct Config {
        int8_t rxPin;
        int8_t txPin;
        int8_t dePin;
        uint32_t baud;
        uint32_t serialConfig;
        uint32_t turnaroundDelayUs;
        bool deActiveHigh;
    };

    explicit Esp32BaseRs485Port(HardwareSerial& serial);

    bool configure(int8_t rxPin,
                   int8_t txPin,
                   int8_t dePin,
                   uint32_t baud,
                   uint32_t serialConfig = SERIAL_8N1,
                   uint32_t turnaroundDelayUs = ESP32BASE_RS485_DEFAULT_TURNAROUND_DELAY_US,
                   bool deActiveHigh = true);
    bool configure(const Config& config);
    bool begin();
    bool isBegun() const;

    size_t writeBytes(const uint8_t* data, size_t len);
    size_t writeBytes(const char* text);
    bool readable();
    int readByte();
    int available();

    void setTurnaroundDelayUs(uint32_t delayUs);
    uint32_t turnaroundDelayUs() const;
    void setDirectionReceive();
    void setDirectionTransmit();
};
```

约定：

- 应用持有 `Esp32BaseRs485Port` 实例并指定 `HardwareSerial`，基础库不抢占 `Serial1`/`Serial2`。
- `configure()` 要求 RX、TX、DE 引脚和 `baud` 有效；`serialConfig` 直接传给 Arduino `HardwareSerial::begin()`，例如 `SERIAL_8N1`。
- `begin()` 会先把 DE 配置为输出并置为接收态，再初始化串口。默认 `deActiveHigh=true`，即 DE 高电平发送、低电平接收；反相收发器可设置为 `false`。
- `writeBytes()` 发送前切到 TX，等待 `turnaroundDelayUs`，写入字节，调用 `HardwareSerial::flush()` 等待 TX 完成，再等待同一 turnaround delay 并切回 RX。
- `readable()`、`available()` 和 `readByte()` 只做轮询读取，不缓存协议帧；没有可读字节时 `readByte()` 返回 Arduino `Stream::read()` 的 `-1`。
- `setDirectionReceive()` / `setDirectionTransmit()` 只在 `begin()` 成功后生效，用于少数需要手动控制方向的应用层时序。
- 模块不分配大缓冲，不创建 Esp32Base 后台任务，不参与 `Esp32Base::handle()`；应用在单 loop 中调用读取和协议状态机。

最小接入：

```cpp
#define ESP32BASE_ENABLE_RS485_PORT 1
#include <Esp32Base.h>

Esp32BaseRs485Port rs485(Serial2);

void setup() {
    Esp32Base::begin();
    rs485.configure(16, 17, 4, 9600, SERIAL_8N1, 50);
    rs485.begin();
}

void loop() {
    Esp32Base::handle();
    if (rs485.readable()) {
        const int value = rs485.readByte();
        // Application protocol parsing belongs here.
    }
}
```

## 10. Esp32BaseWeb

```cpp
class Esp32BaseWeb {
public:
    static constexpr uint8_t MAX_BUSINESS_RECORD_STORES = 8;

    static constexpr const char* EVENT_READY = "web.ready";
    static constexpr const char* EVENT_STOPPED = "web.stopped";
    static constexpr const char* EVENT_TOOLS_FORMAT_FS_SUCCESS = "web.tools.format_fs.success";

    enum Method : uint8_t {
        METHOD_UNKNOWN,
        METHOD_GET,
        METHOD_POST,
        METHOD_ANY
    };

    enum HomeMode : uint8_t {
        HOME_ESP32BASE,
        HOME_APP,
        HOME_COMBINED
    };

    enum SystemNavMode : uint8_t {
        SYSTEM_NAV_TOP,
        SYSTEM_NAV_BOTTOM,
        SYSTEM_NAV_SECTION
    };

    enum FooterBarMode : uint8_t {
        FOOTER_BAR_OFF,
        FOOTER_BAR_STATUS_ONLY,
        FOOTER_BAR_FULL
    };

    enum BuiltinPage : uint8_t {
        BUILTIN_HOME,
        BUILTIN_WIFI,
        BUILTIN_OTA,
        BUILTIN_LOGS,
        BUILTIN_APP_EVENTS,
        BUILTIN_TOOLS,
        BUILTIN_SYSTEM,
        BUILTIN_AUTH
    };

    enum UiTone : uint8_t {
        UI_NEUTRAL,
        UI_OK,
        UI_WARN,
        UI_DANGER,
        UI_INFO
    };

    struct ResultNotice {
        const char* param;
        const char* value;
        UiTone tone;
        const char* title;
        const char* message;
    };

    struct Pagination {
        const char* path;
        const char* query;
        uint32_t page;
        uint32_t perPage;
        uint32_t total;
    };

    struct FormatFsResult {
        const char* source;
        bool formatSuccess;
        bool mountSuccess;
        bool fileLogReloadSuccess;
        uint8_t businessRecordStoreCount;
        uint8_t businessRecordStoreReloadedCount;
        bool businessRecordStoresReloadSuccess;
    };

    using AfterFormatFsCallback = void (*)(const FormatFsResult& result, void* user);

    // Only available when ESP32BASE_WEB_NATIVE_TEST=1 in a non-ESP32 native test build.
    struct NativeTestHeader {
        std::string name;
        std::string value;
    };

    struct NativeTestResponse {
        int code;
        std::string contentType;
        std::string body;
        std::vector<NativeTestHeader> headers;
        bool started;
        bool ended;
    };

    using Handler = void (*)();

    static bool begin();
    static void handle();
    static bool isReady();

    static void setDefaultAuth(const char* user, const char* pass);
    static const char* authUser();
    static const char* authPassword();
    static bool isAuthEnabled();
    static void setAuthEnabled(bool enabled);
    static bool checkAuth();
    static bool checkPostAllowed(const char* context = nullptr);
    static bool verifyAuth();
    static bool verifyAuth(const char* user, const char* pass);
    static bool saveAuth(const char* user, const char* pass);
    static bool resetAuth();

    static bool addRoute(const char* path, Method method, Handler handler);
    static bool addPage(const char* path, const char* title, Handler handler);
    static bool addApi(const char* path, Handler handler);
    static bool addStaticAsset(const char* path, const char* contentType, const uint8_t* data, size_t len,
                               uint32_t cacheMaxAgeSec = 86400, bool authRequired = true);
    static bool addNavItem(const char* path, const char* title);
    static bool registerBusinessRecordStore(Esp32BaseRecordStore& store);

    static bool setDeviceName(const char* name);
    static bool setHomePath(const char* path);
    static void setHomeMode(HomeMode mode);
    static void setSystemNavMode(SystemNavMode mode);
    static bool setFooterBarMode(FooterBarMode mode);
    static FooterBarMode footerBarMode();
    static const char* footerBarModeName();
    static bool setBuiltinLabel(BuiltinPage page, const char* label);
    static void setHeadExtraCallback(Handler handler);
    static void setAfterFormatFsCallback(AfterFormatFsCallback cb, void* user = nullptr);
    static void clearAfterFormatFsCallback();

    static Method currentMethod();
    static bool isMethod(Method method);
    static const char* currentMethodName();

    static bool hasParam(const char* name);
    static bool getParam(const char* name, char* out, size_t len);
    static bool getRequestBody(char* out, size_t len);

    static void sendHeader(const char* title = nullptr);
    static void sendFooter();
    static void sendPageTitle(const char* title, const char* subtitle = nullptr);
    static void beginPanel(const char* title = nullptr);
    static void endPanel();
    static void sendNotice(UiTone tone, const char* title, const char* message = nullptr);
    static void sendResultNotice(const ResultNotice* notices, uint8_t count);
    static void beginMetricGrid();
    static void sendMetric(const char* label, const char* value, const char* help = nullptr);
    static void endMetricGrid();
    static void sendInfoRowCompact(const char* title, const char* help, const char* value = nullptr);
    static void sendInfoRowCompactLink(const char* title, const char* help, const char* value,
                                       const char* href, const char* label, UiTone tone = UI_INFO);
    static void sendInfoRowCompactForm(const char* title, const char* help, const char* value,
                                       const char* action, const char* label,
                                       const char* hiddenName = nullptr, const char* hiddenValue = nullptr,
                                       UiTone tone = UI_INFO);
    static void sendInfoRowInlineEdit(const char* id, const char* title, const char* help, const char* value,
                                      const char* action, const char* inputName, const char* inputValue,
                                      const char* label = "Edit", UiTone tone = UI_INFO);
    static void sendInfoRowDialogForm(const char* dialogId, const char* targetId, const char* title,
                                      const char* help, const char* value, const char* action,
                                      const char* fieldsHtml, const char* label = "Edit", UiTone tone = UI_INFO);
    static void sendPagination(const Pagination& pagination);
    static bool isAjaxRequest();
    static void sendAjaxReplace(const char* targetId, const char* html, const char* noticeTitle = nullptr,
                                UiTone tone = UI_OK, bool close = true);
    static void sendAjaxError(int code, const char* error);
    static bool sendResponseHeader(const char* name, const char* value);
    static bool beginResponse(int code, const char* contentType, const char* filename = nullptr);
    static bool beginText(int code);
    static bool beginCsv(int code, const char* filename = nullptr);
    static void endResponse();
    static void sendChunk(const char* text);
    static void sendBytes(const uint8_t* data, size_t len);
    static void writeHtmlEscaped(const char* text);
    static void writeCsvEscaped(const char* text);
    static void redirectSeeOther(const char* location);

    static void sendText(int code, const char* text);
    static void sendHtml(int code, const char* html);
    static void sendJson(int code, const char* json);
    static void beginJson(int code);
    static void writeJsonEscaped(const char* text);
    static void endJson();

    // Only available when ESP32BASE_WEB_NATIVE_TEST=1 in a non-ESP32 native test build.
    static void nativeTestReset();
    static void nativeTestBeginRequest(Method method, const char* path);
    static void nativeTestSetParam(const char* name, const char* value);
    static void nativeTestSetBody(const char* body);
    static void nativeTestSetAuthenticated(bool authenticated);
    static void nativeTestSetSameOrigin(bool sameOrigin);
    static bool nativeTestRun(Handler handler);
    static bool nativeTestDispatch(const char* path, Method method);
    static const NativeTestResponse& nativeTestResponse();
    static const char* nativeTestResponseHeader(const char* name);
    static void nativeTestNotifyToolsFormatFsSuccess(bool mountSuccess, bool fileLogReloadSuccess);
};
```

Route 缓冲机制：

- `addRoute()` 始终写入静态路由表。
- path 必须以 `/` 开头，最大可见长度为 47 字节；更长路径返回 false，避免静默截断。
- Web 未启动时缓存。
- Web 启动阶段注册一次。
- Web 已启动时立即注册。
- `addPage()` 注册业务页面并默认进入业务导航；`addNavItem()` 可把应用已有路由加入业务导航但不注册 handler。
- `addPage(path, title, handler)` 必须由应用显式提供短标题，例如 `Fan`、`Config`。
- title 最大可见长度为 23 字节；空 title 返回 false。
- `addRoute()` 和 `addApi()` 不进入业务入口列表，避免 API 或隐藏路由污染导航。
- `addStaticAsset()` 注册业务 CSS/JS/图片等固件内固定资源，使用独立固定容量表，不消耗应用 route 表；path 必须以 `/` 开头且最大可见长度为 47 字节，不能位于 `/esp32base/**` 内置命名空间，content type 最大 63 字节，data 指针和 content type 字符串必须在固件生命周期内有效。默认 `authRequired=true`，响应包含固定 `Content-Length`、`X-Content-Type-Options: nosniff` 和 `Cache-Control`；受保护资源使用 `private, max-age=...`，公开资源使用 `public, max-age=...`，`cacheMaxAgeSec=0` 时发送 `no-store`。业务静态资源应优先用该 API 注册，不应绕开 Esp32Base WebServer。
- `registerBusinessRecordStore(store)` 仅在RecordStore启用时有效，用于把当前版本Store交给基础库System页管理。首次 `begin()` 完成后登记一次并检查返回值即可；最多登记8个对象，重复登记同一对象幂等，不同对象登记同一路径会被拒绝。登记表只保存指针且不提供反登记，因此对象在登记后必须持续有效到设备重启。System页不扫描目录、不登记App Events，也不处理未登记历史版本；它统一提供状态展示、全量逻辑清空，并在System页格式化LittleFS后自动 `reload()` 已登记Store。
- `setDeviceName()` 设置导航品牌和默认标题；`setHomePath()` 设置业务首页路径。
- `setHomeMode(HOME_ESP32BASE)` 让 `/` 跳转 `/esp32base/status`；`HOME_APP` 和 `HOME_COMBINED` 只改变裸 `/` 的业务首页行为。`/esp32base/status` 始终是 Status 页，`/esp32base` 与 `/esp32base/` 始终重定向到该规范地址。未显式 `setHomePath()` 时优先使用已注册的 `/index` 业务页作为首页；显式 `setHomePath("/")` 且注册 GET `/` 业务页时，裸 `/` 直接调用业务 handler，不产生自跳转。
- `setSystemNavMode()` 控制系统入口位置：顶部、底部或底部紧凑系统工具区；默认使用 `SYSTEM_NAV_SECTION`，把 Status、System Logs、System 作为小字链接与 `Free heap`、`Up`、`RSSI` 放在同一 footer 区域，窄屏可自然换行；启用 App Config 或 App Events 时系统入口同时显示对应直达链接。System Logs 是 `/esp32base/logs` 的默认用户标签，底层枚举仍是 `BUILTIN_LOGS`。
- `FooterBarMode` 控制 `sendFooter()` 的底部横条输出：`FOOTER_BAR_OFF` 不显示，`FOOTER_BAR_STATUS_ONLY` 只显示运行摘要，`FOOTER_BAR_FULL` 显示系统入口和运行摘要。默认 `FOOTER_BAR_FULL`，System 页面保存后写入 `eb_ui.footer_mode` 并立即影响后续页面输出。
- `setBuiltinLabel()` 覆盖内置导航标签，可用于中文本地化；系统工具页统一使用 `BUILTIN_TOOLS`，不提供旧 Reboot 历史别名。
- `setHeadExtraCallback()` 设置额外 head 输出回调；`sendHeader()` 在默认 `WEB_HEAD` 后、`</head><body>` 和顶部导航前调用它，业务项目可在这里输出 `<style>`，避免页面刷新时先显示基础库默认导航样式。该回调不会注入 `/esp32base` 及其子路径的内置页面，避免业务 CSS 增加内置页体积。
- `setAfterFormatFsCallback()` 注册内置 System 页 `POST /esp32base/system/format-fs` 的 after-format 回调；只有显式 System 页面格式化 LittleFS 成功后触发，格式化失败不触发，启动挂载失败路径不会触发。回调结果会说明格式化来源、重新挂载、FileLog reload，以及已登记业务 Store 的总数、成功 reload 数和整体结果。启用 Bus 时同一动作还会发布 `EVENT_TOOLS_FORMAT_FS_SUCCESS`。已登记 RecordStore 由基础库自动 reload；基础库不会默认清任何业务 NVS，业务如需同步清统计、文件索引或运行时缓存，仍应在该回调或事件订阅里自行处理。
- Web UI helper 只负责轻量 HTML 结构和统一样式，不接管业务数据模型：`sendPageTitle()` 输出页面标题区，`beginPanel()` / `endPanel()` 输出内容分组，`sendNotice()` / `sendResultNotice()` 输出横向状态反馈，`beginMetricGrid()` / `sendMetric()` / `endMetricGrid()` 输出状态和统计摘要，`sendInfoRowCompact*()` 输出紧凑信息行和单动作，`sendInfoRowInlineEdit()` 输出单字段行内编辑，`sendInfoRowDialogForm()` 输出 1-3 字段小表单弹层，`sendPagination()` 输出页码型分页、每页条数和跳页表单。分页 `perPage=0` 时默认 10 条，每页选项为 10、15、20、30、50。`sendInfoRowCompactLink()`、`sendInfoRowCompactForm()`、行内编辑和弹层入口都会按 `UiTone` 输出轻量按钮；页面级明确保存/执行按钮仍可直接使用普通 submit 按钮。
- 带 `data-eb-ajax` 的 helper 表单会在浏览器支持 `fetch` 时携带 `X-Esp32Base-Ajax: 1` 和 `Accept: application/json` 局部提交；服务端用 `isAjaxRequest()` 判断后返回 `sendAjaxReplace(targetId, html, noticeTitle)` 或 `sendAjaxError(code, error)`。未携带 AJAX header 时，同一 POST endpoint 仍应保留 `POST -> 303 -> GET` fallback。
- `UiTone` 仅表达语义色：neutral、ok、warn、danger、info。业务项目不得把危险、警告、成功语义当作普通装饰色复用。
- 导航会给当前匹配项输出 `active` class；匹配规则为 path 完全相等，或当前路径以 `path + "/"` 开头，多个匹配时选择最长 path。`SYSTEM_NAV_SECTION` 下 WiFi/Auth/OTA 二级页会把底部 System 入口标记为 active；App Config 页面在启用时使用自己的底部入口标记 active。
- `/esp32base/status` Status 页是只读设备体检页，采用诊断优先结构，展示设备身份、网络参数、运行健康、存储与日志、NVS 摘要、固件 OTA 和硬件安全摘要。页面不使用展开模式，诊断字段直接平铺；同时不做 LittleFS 全量文件树扫描、文件可读性检查、top files 统计、FileLog 段文件大小统计、完整分区表枚举或全镜像校验。较重的文件细节放在 `/esp32base/fs`。系统设置通过统一 Footer Bar 的 System 入口访问，不在正文重复放置入口卡片。
- `/esp32base/logs` 和 `/esp32base/logs/raw` 是只读系统诊断日志查看入口，只读取已经落盘的 FileLog segment 快照；GET 读取不得主动 `flush()`、创建、清空、重建文件或改变 FileLog fault 状态。INFO 缓存中的新日志按常规 flush interval 落盘，清空/格式化/重启等维护副作用必须通过 POST 路径。
- `/esp32base/fs` 是启用 FS profile 时注册的 LittleFS 诊断页，默认只读，展示容量摘要、主要文件和文件树。文件树区分普通文件、不可读文件和基础库管理文件；不可读文件不能给出会生成 0 字节伪成功的下载入口。当 `FS used` 明显大于可见文件合计时，页面应提示可能存在内部、历史或不可见占用。
- `/esp32base/fs/download?path=/file` 下载一个已存在且可读取的文件，复用 Basic Auth 和路径校验，目录、缺失文件和非法路径不会下载；如果文件声明有大小但无法读取首块，返回 `500 File read failed`。
- `/esp32base/fs?manage=1` 进入单文件删除和受限上传管理模式；`POST /esp32base/fs/delete` 只接受一个已存在文件路径，复用 Basic Auth、同源检查和 `POST -> 303 -> GET`，不提供目录删除、批量删除、编辑或任意路径输入。不可读文件仍允许删除，便于清理损坏文件；删除不以文件内容可读为前提。基础库管理文件会在文件树中给出维护提醒；维护操作修改 FileLog 或 App Events store 后，应刷新对应运行态。
- `/esp32base/fs/check?dir=/data&name=records.bin` 是上传前检查接口，复用 Basic Auth，按“已有目录 + 本地文件名”计算目标路径，返回目标是否存在、是否是目录以及是否允许上传；路径拼接如果超过内部路径上限会直接拒绝，不截断成另一个目标。
- `POST /esp32base/fs/upload` 是 multipart 上传接口，复用 Basic Auth 和同源检查；上传保留本地文件名，可以写入任何已有目录，不创建目录。目标文件存在时必须传 `overwrite=1` 才会覆盖；覆盖上传必须避免上传中断时先清空旧文件。路径过长、缺少文件、目标非法或写入失败会返回错误，不会复用上一笔上传状态。上传只负责写入 LittleFS，不校验业务数据语义、索引、NVS 状态或运行时缓存。
- `/esp32base/system` System 页承载低频维护入口和操作，App Config 启用时作为首个入口显示，后面是 WiFi Setup、Web Auth、Firmware OTA、File system 直达入口、hostname 保存、Watchdog trip reset、重启设备；启用 FS 的 profile 还提供手动格式化 LittleFS 操作，会清除日志和所有 LittleFS 文件，但不清除 WiFi、Web Auth、业务 namespace 或任何 NVS 配置。格式化成功后会重新 mount FS、reload FileLog、重新创建 App Events，并自动 reload 已登记的当前业务 RecordStore；业务仍应通过 after-format 回调或事件同步自己的派生统计、文件索引或缓存。
- `/esp32base/auth` 是内置认证管理页面，受当前 Basic Auth 保护，提交成功后新账号密码立即生效。
- Web Auth 认证优先级为：已保存认证 > 应用默认认证。未设置应用默认认证且没有已保存认证时，Web 服务不会启动。
- 内置 Web 不提供首次登录强制改密；量产或可被他人访问的设备必须在业务启动时调用 `setDefaultAuth()`，或通过业务流程先保存认证。
- 仅显式启用 `ESP32BASE_WEB_ALLOW_INSECURE_DEFAULT_AUTH=1` 的受控开发固件会使用库内置 `admin/admin` 兜底，并输出 WARN 审计日志。
- `setDefaultAuth(user, pass)` 设置应用默认认证；如果用户已保存认证，不会覆盖已保存认证。
- `authUser()` 返回当前用户名；`authPassword()` 返回当前生效密码，仅供本地 C++ 认证集成使用，例如 ArduinoOTA/espota，禁止输出到 HTML、JSON 或 API 响应。
- Basic Auth `Authorization` header 有内部长度上限；超长 header 会被拒绝并输出 WARN，不会静默截断后继续认证。
- `setAuthEnabled(false)` 会完全开放内置 HTTP 路由，包括 WiFi 保存/清除、Auth 保存、重启、System、System Logs clear 和 Web OTA；只适合受控调试网络。
- `checkAuth()` 用于业务页面复用基础库 Basic Auth；返回 `false` 时已发送认证挑战，业务 handler 应直接 return。
- `checkPostAllowed(context)` 用于业务 POST/危险操作复用基础库 Web Auth、POST method 和 Origin/Referer 同源检查；非 POST 返回 `405 Method Not Allowed`，认证失败发送认证挑战，同源失败返回 `403 Forbidden`。返回 `false` 时业务 handler 应直接 return。`context` 只用于安全审计日志。
- `verifyAuth(user, pass)` 校验显式传入的账号密码；无参 `verifyAuth()` 仍表示当前请求是否已认证。
- `saveAuth(user, pass)` 保存 Web Auth 到 `eb_web.auth_user`、`eb_web.auth_pass`，保存后读回校验，并在保存成功后立即切换为新认证；保存失败不会主动清空旧认证。
- `resetAuth()` 清除 `eb_web` 持久化 Auth，并恢复应用默认认证；没有应用默认认证时返回 false，Web 进入无默认认证的锁定状态，除非开发固件显式启用 `ESP32BASE_WEB_ALLOW_INSECURE_DEFAULT_AUTH=1`。
- Web Auth 密码当前持久化到普通 NVS 的 `eb_web.auth_pass`；未启用平台级 flash encryption 时，不应把设备物理存储视为密文凭据库。日志、HTML、JSON 和 API 响应不输出 Web Auth 密码值；日志只输出用户名、来源、结果和 `password_set` 状态。`authPassword()` 仅供本地 C++ 集成使用。
- `METHOD_ANY` 用于同一路径 GET/POST 复用；应用 handler 内可用 `currentMethod()`、`isMethod()` 或 `currentMethodName()` 判断当前请求方法。
- `currentMethod()` 仅在 handler 上下文中返回实际方法：GET 为 `METHOD_GET`，POST 为 `METHOD_POST`；handler 外或未知方法返回 `METHOD_UNKNOWN`。
- `isMethod(METHOD_ANY)` 在有效 handler 请求中返回 true；`METHOD_ANY` 不作为实际请求方法返回。
- `sendResponseHeader(name, value)` 可在 handler 内、响应开始前追加 HTTP 响应头；header name 仅允许字母、数字和 `-`，value 不允许控制字符且最大 127 字节。已进入 chunked 响应后调用会返回 false 并记录 WARN。
- `beginResponse(code, contentType, filename)` 开始通用 chunked 响应，后续使用 `sendChunk()` 输出文本或 `sendBytes()` 输出二进制块，最后必须调用 `endResponse()`。
- `beginText(code)` 等价于 `text/plain; charset=utf-8` chunked 响应；`beginCsv(code, filename)` 等价于 `text/csv; charset=utf-8`，filename 非空时发送 `Content-Disposition: attachment`。`writeCsvEscaped()` 除了 CSV 引号转义，还会对 `= + - @` 等 spreadsheet formula 前缀加 `'`，降低运维人员用 Excel/Sheets 打开导出文件时的公式执行风险。
- `beginResponse()` / `beginText()` / `beginCsv()` 只能在 handler 请求上下文中成功；handler 外、contentType 为空或超过 63 字节、filename 含不安全字符时返回 false 并记录 WARN。
- 长响应的文本、PROGMEM head、二进制块和结束块发送过程中会主动让出调度；正文 data chunk 由基础库无堆分配 writer 输出并在发送前后喂 watchdog。客户端在发送前已经断开时后续 `sendChunk()` / `sendBytes()` 会停止继续输出。`endResponse()` 会在响应未标记为断开时发送最终 0-length chunk，保证 chunked 响应可被 HTTP 客户端正常判定结束。
- 已经完整生成的小 JSON 优先使用 `sendJson(code, json)`；该路径发送固定 `Content-Length`，不进入 chunked 响应状态机。
- CSV 字段必须用 `writeCsvEscaped()` 输出，避免逗号、换行或双引号破坏导出格式。
- `redirectSeeOther(location)` 发送 `303 See Other`，用于 POST 成功后跳转到 GET 页面，避免浏览器刷新重复提交。
- `beginJson(code)` 的状态码必须在 `endJson()` 发送时保留。

Native Web handler 测试：

- `ESP32BASE_WEB_NATIVE_TEST=1` 只用于非 ESP32 的 native 单元测试构建；如果在 ESP32/Arduino 目标上启用会编译失败，避免测试 harness 进入固件运行时代码体积。
- native harness 不启动 Arduino `WebServer`，而是在 `Esp32BaseWeb` helper 层模拟当前请求并捕获响应。业务 handler 仍按真实代码写法调用 `currentMethod()`、`hasParam()`、`getParam()`、`getRequestBody()`、`checkAuth()`、`checkPostAllowed()`、`sendJson()`、`redirectSeeOther()`、`beginResponse()`、`sendChunk()`、`sendBytes()` 和 `endResponse()`。
- `nativeTestBeginRequest(method, path)` 设置当前请求 method/path 并清空本次参数、body 和响应；随后用 `nativeTestSetParam()`、`nativeTestSetBody()`、`nativeTestSetAuthenticated()`、`nativeTestSetSameOrigin()` 设置请求输入和安全检查结果。
- `nativeTestRun(handler)` 直接执行一个 handler；`nativeTestDispatch(path, method)` 会从 native harness 内的 `addRoute()` / `addPage()` / `addApi()` / `addStaticAsset()` 注册表查找并执行匹配 handler 或静态资源，未匹配时捕获 `404 Not found`。
- `nativeTestResponse()` 返回最近一次响应的状态码、content type、body、headers 和 started/ended 状态；`nativeTestResponseHeader(name)` 按大小写不敏感方式读取捕获的响应头。
- native harness 只验证 handler 在 Esp32Base Web helper 层的行为，不模拟浏览器、TCP、chunked wire framing、multipart 上传、ESP32 WiFi、NVS、LittleFS 或 OTA 副作用；这些仍需要 ESP32 示例构建、集成测试或实机验证。

内置页面交互要求：

- WiFi 配置页面必须回显当前 SSID，不回显当前密码；密码留空表示保持已保存密码，显式清空用于开放 WiFi。
- WiFi 配置提交必须校验 SSID 非空；密码允许为空以支持开放 WiFi。
- 重启按钮必须有二次确认，可用浏览器端 JavaScript 实现。
- API 中的字节数可保留 raw bytes；内置页面面向人工查看时优先只显示 KB/MB/B 人性化格式，避免重复。
- `sendHeader()` 输出的默认 input 样式只覆盖文本类控件；checkbox、radio、file、range、color、hidden 等非文本控件不被拉伸成文本输入框。
- `sendHeader()` 的基础样式保持简洁中性：普通链接不默认渲染为蓝色按钮，导航 active 使用浅绿灰背景和深绿灰文字；业务视觉仍推荐通过 `setHeadExtraCallback()` 注入 CSS。

## 11. Esp32BaseAppConfig

仅在 `ESP32BASE_ENABLE_APP_CONFIG=1` 时可用，依赖 Web。启用后必须显式设置：

```cpp
ESP32BASE_APP_CONFIG_MAX_GROUPS
ESP32BASE_APP_CONFIG_MAX_FIELDS
```

容量硬上限为 16 组、128 字段；业务 namespace 不能使用 `eb_` 前缀。注册必须在 `Esp32Base::begin()` 前完成。注册结构体中的字符串指针和 enum option 数组只被引用、不被复制，必须在固件生命周期内一直有效，推荐使用 `static const`。

职责：

- 业务注册可持久化修改的应用参数。
- 基础库在 `/esp32base/app-config` 输出紧凑配置页。
- 保存前显示变更核对清单。
- 后端重新解析、校验、读取当前值并只保存变化字段。
- `restartRequired` 字段保存后，在当前未重启会话内持续显示运行中旧值和已保存新值；极低内存下 string/enum 旧值可能显示为 `unavailable`，提示仍保留到重启。

公开 API 位于 `web/Esp32BaseAppConfig.h`，统一通过 `#include <Esp32Base.h>` 暴露。字段使用结构体注册，避免长参数列表。支持类型：

- `StringField`：`minLength`、`maxLength`，最大 256 bytes。
- `IntField`：`minValue`、`maxValue`、`step`。
- `DecimalField`：定点数，底层保存 `int32_t raw`，`scale=0..6`。
- `BoolField`。
- `EnumField`：option value 最大 31 bytes。

显示文本限制：group title、字段 label、enum option label 最大 31 bytes；help 最大 192 bytes；unit 最大 12 bytes。长度按 UTF-8 编码后的字节数计算；超限输入拒绝注册且不静默截断。

回调：

- `FieldValidateCallback`：字段级业务校验，例如只允许英文数字。
- `PageValidateCallback`：页面级跨字段校验，也是整页保存前 veto hook；回调中可用 `submittedString()`、`submittedInt()`、`submittedDecimal()`、`submittedBool()`、`submittedEnum()` 读取本次 POST 值；string buffer 至少应为 `STRING_MAX_LENGTH + 1`，enum buffer 至少应为 `ENUM_VALUE_MAX_LENGTH + 1`，buffer 过小时会返回 false。
- `ChangeCallback`：字段成功保存后调用，包含旧值和新值；其中 `text` 指针只在回调期间有效。
- `SaveCallback`：整次保存结束后调用，提供 changed/saved/failed 统计和是否涉及重启后生效字段；`SaveCallback` 是保存后通知，不用于拒绝保存。

保存语义：

- GET 只读，不产生副作用。
- POST 必须通过 Web Auth、POST method 和 Origin/Referer 同源检查。
- 页面带 RAM revision；过期页面提交会被拒绝并要求刷新。
- 任一字段或页面级校验失败时不写任何字段，也不触发 change 回调。
- 只读状态、未来配置版本或业务版本不兼容等需要拒绝整页保存的业务规则，应放在 `PageValidateCallback`；回调返回 false 时不会进入字段写入循环，也不会触发 `ChangeCallback` 或 `SaveCallback`。
- 校验全部通过后，只写入后端计算出的变化字段；未变化字段不写 NVS。
- 部分 NVS 写失败时，已成功字段保留，只对成功字段触发 change 回调。

示例：

```cpp
const Esp32BaseAppConfig::EnumOption MODES[] = {
    {"auto", "Auto"},
    {"manual", "Manual"}
};

bool validateName(const Esp32BaseAppConfig::FieldRef&, const Esp32BaseAppConfig::FieldValue& value, char* error, size_t errorLen) {
    for (const char* p = value.text; p && *p; ++p) {
        const char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            strlcpy(error, "Only letters and digits are allowed.", errorLen);
            return false;
        }
    }
    return true;
}

void setup() {
    Esp32BaseAppConfig::setTitle("App Config");
    Esp32BaseAppConfig::addGroup({"main", "Main"});
    Esp32BaseAppConfig::addString({"main", "app_cfg", "name", "Name", "device1", 1, 32, nullptr, false, validateName});
    Esp32BaseAppConfig::addInt({"main", "app_cfg", "limit", "Limit", 50, 0, 100, 1, nullptr, nullptr, false, nullptr});
    Esp32BaseAppConfig::addEnum({"main", "app_cfg", "mode", "Mode", "auto", MODES, 2, nullptr, true, nullptr});
    Esp32Base::begin();
}
```

## 12. Esp32BaseOta

```cpp
class Esp32BaseOta {
public:
    static constexpr const char* EVENT_START = "ota.start";
    static constexpr const char* EVENT_PROGRESS = "ota.progress";
    static constexpr const char* EVENT_SUCCESS = "ota.success";
    static constexpr const char* EVENT_FAILED = "ota.failed";

    enum Status : uint8_t {
        IDLE,
        READY,
        UPLOADING,
        VERIFYING,
        SUCCESS,
        FAILED
    };

    static bool begin();
    static bool beginNetworkServices();
    static void handle();
    static bool isReady();
    static bool isUploading();
    static Status status();

    static bool startUpload(size_t totalSize, const char* expectedSha256Hex = nullptr);
    static bool writeChunk(const uint8_t* data, size_t len);
    static bool finishUpload();
    static void abortUpload(const char* reason);

    static uint8_t progress();
    static size_t bytesProcessed();
    static size_t totalSize();
    static const char* lastError();
    static const char* runningOtaState();
    static bool waitingForMarkValid();
    static uint32_t markValidElapsedMs();
    static uint32_t markValidTimeoutMs();

    static bool markCurrentValid();
    static bool isRollbackPossible();
    static void rollbackAndRestart(const char* reason);
};
```

`begin()` 只初始化 OTA boot 诊断和 rollback/mark-valid 状态，应在系统启动早期执行；`beginNetworkServices()` 在 Web/Auth ready 后启动 ArduinoOTA/espota 网络入口。普通业务通常不需要直接调用这两个函数，由 `Esp32Base` 统一调度。

`runningOtaState()` 返回当前 running partition 的 OTA state 字符串：`valid`、`pending_verify`、`aborted`、`invalid`、`undefined`、`new`、`unknown` 或 `n/a`。`waitingForMarkValid()` 仅在启用 `ESP32BASE_OTA_REQUIRE_MARK_VALID=1` 且当前 running partition 为 `pending_verify` 时返回 `true`。业务自检通过后调用 `markCurrentValid()`；如果当前镜像不是 `pending_verify`，该函数会记录诊断并返回 `false`，不会误报确认成功。

`Update.end(true)` 必须在 SHA256 校验通过、且实际接收字节数等于声明总大小之后调用。

`expectedSha256Hex` 可为空；不为空时必须是 64 字节十六进制字符串，大小写均接受，内部统一按小写比较。

Web OTA 上传页面不要求额外认证；它只复用 Web 层 Basic Auth。上传过程必须提供进度展示。

启用 `ESP32BASE_ENABLE_OTA` 时，`ESP32BASE_ENABLE_ARDUINO_OTA` 默认同为 1，提供 ArduinoOTA/espota 兼容入口。命令行 OTA 使用 `Esp32Base::hostname()` 和标准端口 3232，复用 mDNS 的 `<hostname>.local` 解析；认证密码为当前生效的 Web Auth 密码，用户名不参与。Web/API 修改 hostname 后必须重启，ArduinoOTA 和 mDNS 才会使用新名称。即使 Web Auth 被关闭，ArduinoOTA 仍要求密码。

Web OTA 与 ArduinoOTA 不得同时写 flash；已有 OTA 传输进行时，另一入口必须拒绝或暂停处理。

## 13. Esp32BaseWatchdog / Sleep / Fs / Health

```cpp
class Esp32BaseWatchdog {
public:
    static bool begin(uint32_t timeoutMs);
    static void feed();
    static bool enterLongOperation();
    static bool exitLongOperation();
    static bool currentTaskInLongOperation();
    static bool isEnabled();
    static bool wasWatchdogReset();
    static uint32_t lifetimeResetCount();
};

class Esp32BaseSleep {
public:
    static bool begin();
    static bool deepSleepSeconds(uint32_t seconds);
    static bool deepSleepUs(uint64_t us);
    static bool enableTimerWakeup(uint64_t us);
    static bool enableGpioWakeup(int gpio, int level);
};
```

`Esp32BaseWatchdog::begin(timeoutMs)` 要求 `timeoutMs >= 1000`；更小值返回 false 并输出 WARN，避免 Arduino ESP32 2.x 下秒级 WDT 参数被截断为 0。

`enterLongOperation()` / `exitLongOperation()` 用于 OTA、FileLog 轮转等预期较长的 flash 操作；`currentTaskInLongOperation()` 可在嵌套长操作前检查当前任务是否已经处于基础库长操作范围。
这些接口按当前 FreeRTOS task 和嵌套深度记录长操作状态，并不把当前 task 从 WDT 注销；只供基础库内部同步 Flash/FS/OTA 等长操作使用，不是业务关闭 Watchdog 的通用开关。

`begin()` 是轻量初始化，只记录 Sleep 模块进入统一生命周期管理，不配置任何默认 wake source。

`deepSleepSeconds()` / `deepSleepUs()` 不是 IDF deep sleep API 的 thin wrapper。它们必须走统一生命周期流程：

1. 发布生命周期事件，如 Bus 启用。
2. 写重启日志环。
3. `Esp32BaseConfig::flushAll()`。
4. 输出最后诊断。
5. 处理 Watchdog。
6. 调用 `esp_deep_sleep_start()`。

用户代码禁止直接调用 `esp_deep_sleep_start()` 绕开库流程。

```cpp
class Esp32BaseFs {
public:
    struct EntryInfo {
        const char* name;
        size_t size;
        bool isDir;
        uint32_t modifiedEpoch;
    };

    static bool begin();
    static bool isReady();
    static bool format();

    // text
    static bool writeFile(const char* path, const char* content);
    static bool readFile(const char* path, char* out, size_t len);
    static bool appendFile(const char* path, const char* content);

    // binary
    static bool writeBytes(const char* path, const uint8_t* data, size_t len);
    static bool readBytes(const char* path, uint8_t* out, size_t maxLen, size_t* readLen);
    static bool appendBytes(const char* path, const uint8_t* data, size_t len);
    static bool createFixedFile(const char* path, uint32_t size, uint8_t fillByte = 0);
    static bool readBytesAt(const char* path, uint32_t offset, uint8_t* out, size_t maxLen, size_t* readLen);
    static bool writeBytesAt(const char* path, uint32_t offset, const uint8_t* data, size_t len);

    // metadata
    enum RemoveFileResult : uint8_t {
        REMOVE_FILE_FAILED,
        REMOVE_FILE_DELETED,
        REMOVE_FILE_CLEARED
    };
    static RemoveFileResult removeFileWithRecovery(const char* path);
    static bool removeFile(const char* path);
    static bool rename(const char* from, const char* to);
    static bool exists(const char* path);
    static int64_t fileSize(const char* path);

    // directory
    using ListCallback = void (*)(const char* name, size_t size, bool isDir, void* user);
    using ListInfoCallback = void (*)(const EntryInfo& entry, void* user);
    static bool listDir(const char* path, ListCallback cb, void* user = nullptr);
    static bool listDirInfo(const char* path, ListInfoCallback cb, void* user = nullptr);
    static bool mkdir(const char* path);
    static bool rmdir(const char* path);

    // capacity
    static size_t totalBytes();
    static size_t usedBytes();
    static size_t freeBytes();
    static bool storageInfo(size_t& total, size_t& used);
};

class Esp32BaseHealth {
public:
    static constexpr const char* EVENT_TICK = "health.tick";

    static bool begin();
    static void handle();
    static uint32_t lastTickMs();
    static uint32_t loopPeriodMaxMs();
};
```

`format()` 会格式化 LittleFS 分区并清除已有文件；基础库默认 `begin()` 不自动格式化，避免误删业务数据。示例工程可在确认是首次调试或可丢弃文件内容时显式调用。

FS 未成功 `begin()` 时，文件和目录操作返回失败，容量查询返回 0，`storageInfo(total, used)` 返回 false 并把输出置 0，不隐式格式化文件系统。需要同时读取总容量和已用容量时优先使用 `storageInfo(total, used)`；它用一次底层 LittleFS 信息查询返回两个值，避免状态页或业务摘要分别调用 `totalBytes()` / `usedBytes()` 时重复查询。

`listDirInfo()` 遍历目录时返回文件名、大小、目录标记和 `modifiedEpoch`。`modifiedEpoch` 来自 Arduino `File::getLastWrite()` / LittleFS 最后修改时间，不是创建时间；只有文件写入时设备系统时间可信时才代表真实日期。`listDir()` 是保留给只需要 name/size/isDir 的轻量回调，会复用同一枚举路径。目录遍历在每次 callback 后显式关闭当前 `File`，避免长目录遍历时积累底层句柄。

Offset binary API 用于业务二进制定长记录、分页读取和环形覆盖写入，不要求业务 include `LittleFS.h` 或 Arduino `File`：

- `readBytesAt()` 打开已存在文件并从 `offset` 读取最多 `maxLen` 字节，实际读取长度写入 `readLen`；读到 EOF 前允许短读并返回 true。
- `readBytes()` / `readBytesAt()` 在 FS 未 ready、path 非绝对路径、文件不存在、out 为空或 `offset > fileSize` 时返回 false；失败时 `readLen` 为 0。`offset == fileSize` 返回 true 且读取 0 字节。底层声明文件仍有剩余内容但读出 0 字节时，`readBytes()` / `readBytesAt()` 返回 false，避免把 LittleFS 元数据仍存在但内容块不可读的文件误判为成功读取。
- 大块读写会在 Esp32BaseFs 层分块并定期让出调度和喂 watchdog。`readBytes()`、`readBytesAt()`、`writeBytes()`、`appendBytes()`、`writeBytesAt()` 和 `createFixedFile()` 都走同一长操作处理，不要求业务存储类重复分块。
- `writeFile()` / `writeBytes()` / `appendFile()` / `appendBytes()` 会在写入后 flush 并校验最终大小；非空写入还会确认写入后文件末端可读。`appendBytes()` 面向低频追加，不适合用循环追加来初始化大容量定长文件。写入失败不等于已回滚，调用方必须按返回值处理可能的部分写入或文件不可读状态。
- `createFixedFile(const char* path, uint32_t size, uint8_t fillByte = 0)` 用于固定容量二进制文件初始化。FS 未 ready、path 非绝对路径或底层创建/写入/校验失败时返回 false；`size == 0` 会创建或截断为空文件。文件不存在、大小不匹配，或同尺寸但末端不可读时会用 `fillByte` 分块重建；已存在且大小一致、末端可读时返回 true 并保留原内容，避免业务每次启动清空持久环形记录。重建过程按长 FS 操作处理 watchdog，并校验最终文件可读。
- `writeBytesAt()` 只覆盖已存在文件中的现有字节，不隐式创建文件、不扩展文件、不填洞；FS 未 ready、path 非绝对路径、文件不存在、data 为空但 len 非 0、`offset + len > fileSize`、底层写失败、写后大小不符或写入范围不可读时返回 false。
- `removeFileWithRecovery()` 在 `LittleFS.remove()` 失败后会尝试把目标文件截断为 0，并用 `REMOVE_FILE_DELETED`、`REMOVE_FILE_CLEARED`、`REMOVE_FILE_FAILED` 明确区分硬删除、仅清空和失败。`removeFile()` 只在硬删除成功时返回 true；维护页面使用恢复型枚举显示 `File deleted or cleared`，业务代码不应把“清空成功”误判为“文件已不存在”。
- 需要固定容量环形文件时，应用应优先用 `createFixedFile()` 初始化或校验文件容量，再用 `writeBytesAt()` 覆盖记录槽位。
- 业务代码必须检查所有 FS API 返回值；如果连续返回 false，不能继续假设记录已写入，应进入降级、清理或提示维护流程。
- 系统诊断日志的实现/API 名称是 `Esp32BaseFileLog`。FileLog 会把追加、清空和轮转截断视为可能较慢的 FS 操作处理；即使 FS 已满或损坏，系统级 WARN 日志写入失败也不应导致 task WDT 重启。检测到 FileLog 写入故障后，会先在运行时停止继续写 FileLog，避免后续 WARN 重复冲击异常文件系统；Web Status/System Logs/System 页显示为 `write fault` 而不是 `disabled`，表示配置仍开启但运行保护停写。配置开启但 FS/init 前置条件不满足时显示 `unavailable`；只有模式 OFF 才显示 `disabled`。Web 清理、格式化或重新保存模式后可重新启用模式。

Health 仅负责周期性采样、loop 周期统计和发布 `health.tick` 事件。heap、reset reason、WiFi state、FS state 等详情通过对应模块查询，Health 不重复包装所有字段。

Health tick 日志策略：

- 每个 tick 窗口内统计一次最大 loop 间隔。
- 未超过 `ESP32BASE_HEALTH_LOOP_WARN_MS` 时，默认每 30 分钟以 DEBUG 输出一次 `tick loopMax=...`，其中 `loopMax` 是这段 DEBUG 日志周期内普通 tick 窗口的最大值。
- 超过阈值时，以 INFO 输出 `loop_slow loopMax=... threshold=...`。
- 默认慢循环阈值为 3000ms，避免普通 Web 请求造成健康日志刷屏，同时保留现场 INFO 诊断入口；业务项目可按控制实时性要求覆盖为 1000/2000/5000ms。
- `ESP32BASE_HEALTH_DEBUG_LOG_INTERVAL_MS` 默认 1800000ms；设为 0 可关闭普通 DEBUG tick 日志，不影响 INFO 慢循环日志。
- `loopPeriodMaxMs()` 仍返回启动以来最大 loop 间隔，不随 tick 窗口清零。
