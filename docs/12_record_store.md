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
- 可选 `minimumFileSystemFreeBytes`。

每条记录由20字节公共元数据、固定payload和4字节CRC32组成，因此槽位大小为 `payloadSizeBytes + 24`。公共元数据保存32位记录ID、完成时间、boot ID、uptime和持续时间。应用不得直接保存含指针、`String`、编译器padding或平台相关布局的对象。

每个版本目录为：

```text
/esp32base/records/<record-type>.v<store-version>/
```

控制文件使用128字节双头；段头32字节。记录顺序追加，重要记录在API返回成功前完成flush/close和写后验证。断电留下的尾部不完整槽位不会返回，其ID也不会重用。

## 3. 分段和轮转

Store按预算和槽位大小从8、16、32、64 KiB级别中选择段上限，使常见32～512 KiB预算通常维持不超过16个完整/尾段文件；很小的测试预算仍使用最小可容纳段。段文件只追加；容量满时删除最老完整段，不逐条搬移、不后台compaction、不因ACK改写记录。

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

统一清空先预检所有Store。预检失败时零修改；执行中I/O失败时停止并返回已完成数量。多个Store之间不提供事务原子性。逻辑清空提交新的可见边界并保持ID继续递增，不保证物理安全擦除。

格式化通过 `Esp32BaseStorage::formatAndReload()` 在独占维护区间完成：flush FileLog、format、mount、FileLog begin、逐个Store reload。Web System页使用同一流程；应用的after-format回调只负责自己的派生缓存或非受管文件。

## 6. 路径所有权与并发

`/esp32base/**` 是基础库受管根。Web文件管理不得上传、覆盖或删除它，也不得修改FileLog轮转文件或已登记Store路径。普通业务文件应位于 `/app/**`、`/data/**` 或项目目录，并以 `unmanagedWritableBytes()` 为上传/创建上限。

所有LittleFS调用通过 `Esp32BaseFs` 的递归串行化保护。格式化等多步维护持有独占维护状态；OTA期间暂停新的FS写入，结束、失败或中止后恢复。锁只解决底层并发，不改变Store对象的调用契约：同一Store的append/read/reload/clear仍应集中在同一loop/system task。ISR、timer和实时控制任务只投递轻量消息，不直接执行Flash操作。

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
