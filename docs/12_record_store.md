# Record Store 与统一存储协调

本文定义 `Esp32BaseStorage`、`Esp32BaseRecordStore` 和 `Esp32BaseConditions` 的职责、容量与接入规则。API签名见 [API契约](03_api.md)，已知限制见 [已知限制](10_known_limitations.md)。

## 1. 分层

持久化分为三层：

1. `Esp32BaseStorage`：协调LittleFS访问、容量、受管路径、格式化恢复和OTA写暂停；不理解业务Schema。
2. 多个独立 `Esp32BaseRecordStore`：每种主要业务事实一个固定payload Store；互不混存、互不建立全局索引。
3. `Esp32BaseConditions`：用NVS位图保存最多32个持续异常的当前活动状态；不保存历史。

`Esp32BaseFileLog` 仍是独立技术日志引擎。它不进入业务Store、不作为业务记录上传，也不由Conditions替代。

## 2. RecordStore 数据模型

应用定义：

- `recordTypeName`：稳定的小写ASCII类型名；
- `storeVersion`：当前格式版本；
- `payloadSizeBytes`：固定业务payload长度；
- `maximumStoreBytes`：该Store的逻辑预算；
- 可选 `minimumFileSystemFreeBytes`；
- `retentionPolicy`：普通历史默认 `RotateOldest`，可靠消费选 `PreserveUnreleased`。

每条记录由20字节公共元数据、固定payload和4字节CRC32组成，因此槽位大小为 `payloadSizeBytes + 24`。公共元数据保存32位记录ID、完成时间、boot ID、uptime和持续时间。应用不得直接保存含指针、`String`、编译器padding或平台相关布局的对象。

每个版本目录为：

```text
/esp32base/records/<record-type>.v<store-version>/
```

当前容器格式为2，控制文件使用128字节双头，持久保存16字节存储世代、保留策略和释放检查点；段头32字节。不自动读取/迁移格式1，不自动清空不匹配存储。记录顺序追加，重要记录在API返回成功前完成flush/close和写后验证。断电留下的尾部不完整槽位不会返回，其ID也不会重用。

## 3. 分段和轮转

Store按预算和槽位大小从8、16、32、64 KiB级别中选择段上限，使常见32～512 KiB预算通常维持不超过16个完整/尾段文件；很小的测试预算仍使用最小可容纳段。段文件只追加；容量满时按保留策略删除可淘汰的最老完整段，不逐条搬移、不后台compaction、不因ACK改写记录。

常见规划结果：

| Store预算 | 常见段上限 | 说明 |
| --- | ---: | --- |
| 64 KiB | 约8 KiB | 小型审计或低频事实 |
| 128 KiB | 约16 KiB | Small档单个子Store |
| 256 KiB | 约32 KiB | Small档全部业务预算 |
| 384 KiB | 约32 KiB | 大型主Store，通常约13段 |
| 512 KiB | 约64 KiB | Large档单Store，通常约9段 |

具体容量必须读取 `StoreStatus.capacity`，不能只用预算除以槽位；控制文件、段头和最后尾段会影响结果。例：728字节业务payload对应752字节槽位，384 KiB预算至少可容纳约516条，512 KiB预算至少约688条。

## 4. 整机容量

默认：

```cpp
#define ESP32BASE_RECORD_STORE_TOTAL_MAX_BYTES (512UL * 1024UL)
#define ESP32BASE_FS_MINIMUM_SAFETY_RESERVE_BYTES (128UL * 1024UL)
```

`Esp32BaseStorage` 校验：

- 最多登记8个Store；
- 所有已登记Store的 `maximumStoreBytes` 合计不超过512 KiB；
- FileLog预算 + Store合计预算 + `max(128 KiB, LittleFS总量/4)` 不超过分区；
- 非受管文件可写容量要再扣除各Store尚未使用的保留预算。

推荐产品档位：

- Small：所有业务Store合计256 KiB；例如两个128 KiB Store。
- Large：所有业务Store合计512 KiB；例如384 KiB主记录 + 128 KiB紧凑审计，或单个512 KiB主Store。

对896 KiB LittleFS，典型规划是FileLog 128 KiB、业务Store最多512 KiB，其余约256 KiB用于安全余量、元数据和临时维护。容量不是运行时动态数据库配额；应用应在设计阶段确定少量固定Store。

## 5. 登记与生命周期

```cpp
Esp32BaseRecordStore wateringStore;
Esp32BaseRecordStore::StoreDefinition definition;
definition.recordTypeName = "watering";
definition.storeVersion = 1;
definition.payloadSizeBytes = sizeof(WateringPayloadV1);
definition.maximumStoreBytes = 384UL * 1024UL;

const bool ready = wateringStore.begin(definition);
const bool registered = Esp32BaseStorage::registerRecordStore(wateringStore);
```

登记对象必须持续有效到重启。重复登记同一对象幂等；重复路径、无效Store、超过数量或预算都会拒绝。只登记当前版本；协调层不扫描目录、不自动处理历史版本。

统一清空先预检所有Store。预检失败时零修改；任何保护模式Store仍有未释放记录时返回 `RecordsProtected`，其他Store也不会先被清空；执行中I/O失败时停止并返回已完成数量。多个Store之间不提供事务原子性。逻辑清空提交新的可见边界并保持ID继续递增，不保证物理安全擦除。

格式化通过 `Esp32BaseStorage::formatAndReload()` 在独占维护区间完成：flush FileLog、format、mount、FileLog begin、逐个Store reload。Web System页使用同一流程；应用的after-format回调只负责自己的派生缓存或非受管文件。

## 6. 路径所有权与并发

`/esp32base/**` 是基础库受管根。Web文件管理不得上传、覆盖或删除它，也不得修改FileLog轮转文件或已登记Store路径。普通业务文件应位于 `/app/**`、`/data/**` 或项目目录，并以 `unmanagedWritableBytes()` 为上传/创建上限。

所有LittleFS调用通过 `Esp32BaseFs` 的递归串行化保护。格式化等多步维护持有独占维护状态；OTA期间暂停新的FS写入，结束、失败或中止后恢复。锁只解决底层并发，不改变Store对象的调用契约：同一Store的append/read/reload/release/checkpoint/clear仍应集中在同一loop/system task。ISR、timer和实时控制任务只投递轻量消息，不直接执行Flash操作。

## 7. Conditions 与审计历史

`Esp32BaseConditions` 只保存 `eb_conditions.active_bits` 当前活动位图。应用长期持有一个 `ConditionTracker`，通过激活/恢复确认过滤瞬态；Unknown取消未完成确认。只有NVS提交成功才返回 `Activated` 或 `Recovered`。

如果产品需要追溯异常发生/恢复，应用在收到成功转换结果后，把紧凑事实写入独立审计Store。这样当前状态和历史事实职责清楚：

- NVS位图负责重启后立即知道哪些条件仍活动；
- 审计Store负责可选历史；
- 主业务Store负责完整业务结果；
- FileLog负责技术原因。

基础库不再提供通用应用事件Store、事件Web页、事件JSON/CSV或隐式事件Schema。

## 8. 失败边界

- 单槽CRC错误：Store进入 `Degraded`，跳过损坏槽，其他有效记录仍可读。
- 写入或写后验证失败：进入 `WriteFault`；排除原因后显式 `reload()`。
- 控制头、定义、段范围或目录结构故障：进入 `StructuralFault`，不自动清空。
- Conditions NVS写失败：返回 `StateWriteFailed`，RAM活动位图不改变；应用不得把它当成成功转换写审计历史。
- Storage预算或维护冲突：通过 `StorageError` 明确返回，不绕过协调层直接操作LittleFS。

存储失败只表示历史事实未可靠提交，不应自动解释成泵阀、接触器或业务动作本身失败；反之业务动作失败也不代表存储一定失败。应用必须分别建模和报告。

## 9. 可靠消费与普通历史共用同一Store

`PreserveUnreleased` 从创建时启用；累计 `releaseThrough(id)` 仅改变RAM，既不删除记录，也不逐确认写Flash。消费者应批量或低频调用 `checkpointRelease()`；记录轮转会在删除前保存必要检查点和下一个ID。写失败不删除；容量被未释放记录占满时明确拒绝追加，不扩大队列或静默换世代。共享同一段的记录必须全部释放才允许淘汰。

`storageGeneration` 是128位UUIDv4布局的存储世代，和记录ID一起标识不可变事实，普通重启、OTA、轮转和逻辑clear保持不变。明确格式化/重建会产生新世代，必须作为破坏性维护单独授权。较早检查点恢复可能要求重复消费；消费者负责幂等、缺号/损坏处理、确认合法性与低频检查点时机。平台主题、JSON、record-ack和补发调度均不进入本库。

详细API、二进制字段及状态见 [RecordStore契约](03_api.md#35-esp32baserecordstore)。

可靠同步接入必须区分本地存储 ID 与外部连续序号：本库在不完整写入后保留 ID 空洞，不能把 `recordId` 无条件直接映射为要求无缺号的服务端累计确认序号。不得对 `Corrupt`/缺号伪造成功确认或静默切换存储世代。接入层必须先明确同一份持久记录中的序号编码、确认到本地释放水位的映射及损坏停机策略，再声明可可靠补发。

已登记的保护模式 Store 在 OTA 暂停 FS 写入前、正常重启和 deep sleep 前，由既有 Storage/facade 生命周期自动尝试保存已推进的释放检查点；未推进时不写。该入口只在 loop/system task 调用，不增加后台任务或常驻缓冲。检查点失败记录 Store 路径及错误，保留 Store 写故障，不阻断 OTA 或安全重启；较早检查点恢复后的重放由消费者幂等处理。运行中继续消费前须排除存储故障并 `reload()`。未登记 Store 由调用方自己管理。

`setBeforeNetworkStopCallback()` 仍仅用于非阻塞网络通知，不能在其中写检查点；OTA 调用该通知前已暂停 FS 写入。普通批量检查点仍由消费者管理，不应依赖设备必定正常关机。

对于要求连续确认序号的平台，SDK 应把其连续序号随原始事实一起编码在同一个 Store 的 payload 中；一次追加、同一 CRC 保护，不另建持久队列或逐条映射文件。Base 的 `recordId` 只作为本地读取和释放游标，平台序号不是它的别名。SDK 必须从持久事实恢复序号，处理写入结果不确定性，并把已验证的连续确认映射回对应本地释放边界；损坏/无法判定时停止该流并诊断，不跳过、不复用可能已经上传的序号、不静默换流。这是上层接入约束，Base 不编码平台字段；SDK 的实现和故障验证尚未完成。

`examples/record_store_demo` 使用保护模式，以 Serial 输出演示本地消费，每32条消费一次检查点；重启可能重复输出。它逐条读取并在缺号/损坏时停止，不把 MQTT 入队、PUBACK 或打印行为描述为平台提交确认。已有旧示例 Store 使用默认轮转策略时会因定义不匹配被拒绝，需按升级规则明确处理旧数据，不能自动清空。

状态读取的总量、已用量和剩余空间取自一次 FS 容量查询，不重复查询或引入常驻缓存；查询失败时这三个容量字段均为0，不影响独立的 Store 状态/错误信息。
