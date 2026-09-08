# 基础能力验证结果

验证日期：2026-09-08。以下为本次 Esp32Base 代码与示例的验证范围，不代表 ESP8266、平台 SDK 或真实设备验收完成。

## 已验证行为

- 看门狗复用系统策略并注册调用任务；重复调用、错误任务、SDK 初始化与注册失败不伪装成功。
- OTA 在参数、存储和长操作准备成功后、`Update.begin()` 前调用 facade 网络暂停 hook；该暂停入口不派发积压的 MQTT 业务 callback。开始/写入/结束失败、摘要不匹配、拒绝/中止与无进展超时释放资源；LOCAL 不依赖 MQTT。
- MQTT 断连清理未交付的完整消息和未完成分片，保留已进入 callback 的消息所有权。512B 接收与 4096B 发送的组合可分别受限；默认保护缓冲、任务栈、重试和存储余量未缩减。
- 统一时间快照跟随系统 UTC 校正，单调 uptime 独立；非法 UTC 与回填整数溢出不伪造有效时间。
- Web 流式响应共用时间预算，部分写入、零进展、慢速持续写入、断连与 `millis()` 回绕均有定向测试。
- 示例使用明确的根库符号链接及内置依赖，不再将整个仓库作为库搜索目录。MQTT 示例默认不跳过证书有效期校验。

生产代码定向测试：`scripts/test_watchdog.py`、`scripts/test_ota_lifecycle.py`、`scripts/test_web_write.py`；PlatformIO 原生测试：`native_time_harness`、`native_time_pcf8563_harness`、`native_mqtt_harness`、`native_mqtt_large_tx_harness`、`native_mqtt_secure_default_harness`、`native_web_harness`。这些使用假的 SDK、网络、存储或时钟；OTA 测试摘要桩只验证不匹配分支，不是密码算法验收。证书来源、构建和证书测试另见 [TLS 工具链](14_tls_toolchain.md)。

Record Store 的 31 项原生测试通过：未释放数据在容量满、缩预算及清空时受保护；释放仅更新 RAM，显式检查点及轮转前落盘，无变化不重复写；控制写失败不先删除记录，新段创建失败后不复用 ID；持久世代在重载/清空后保持。双头损坏回退、旧格式拒绝及多 Store 清空预检也有定向覆盖。原生文件系统是测试桩，不替代真实掉电试验。

## 实际构建与静态体积

Core 2 为 2.0.16，Core 3 为 3.3.8；均通过仓库隔离的 PlatformIO home。以下是最终 ELF 的静态 RAM 和固件 Flash 字节数，不是运行堆、任务栈峰值或 TLS 握手峰值。

| 示例 / 芯片 / Core | Profile | 静态 RAM | Flash |
| --- | --- | ---: | ---: |
| basic / ESP32 / Core 2 | MINIMAL | 22,224 | 273,601 |
| basic / ESP32 / Core 2 | OFFLINE | 23,560 | 325,053 |
| basic / ESP32 / Core 2 | LOCAL | 57,052 | 983,817 |
| basic / ESP32-S3 / Core 2 | LOCAL | 55,840 | 940,489 |
| basic / ESP32-C3 / Core 2 | LOCAL | 49,796 | 991,034 |
| basic / ESP32 / Core 3 | LOCAL | 59,172 | 1,180,653 |
| mqtt_tls 受控包探针 / ESP32 / Core 3 | IOT | 62,024 | 1,295,388 |
| full_demo / ESP32 / Core 2 | LOCAL | 58,508 | 1,014,149 |
| record_store_demo / ESP32 / Core 2 | MINIMAL + FS/Record Store | 25,648 | 337,049 |
| record_store_demo / ESP32-S3 / Core 2 | MINIMAL + FS/Record Store | 22,604 | 330,433 |
| record_store_demo / ESP32-C3 / Core 2 | MINIMAL + FS/Record Store | 17,936 | 315,292 |
| record_store_demo / ESP32 / Core 3 | MINIMAL + FS/Record Store | 25,996 | 346,272 |

受控 IOT 示例此前同场景为 RAM 63,400B / Flash 1,323,372B；本次代码与明确依赖共同变更后分别减少 1,376B / 27,984B，不能把差值归因于某一个函数。4KiB 发送、512B 接收时，2 个接收 payload 数组比共用 4KiB 容量少 7,168B，属于可推导的静态布局差值，不是该默认示例的实测节省。

其余示例 `web_ui_gallery`、`web_logs_ota`、`net_runtime`、`rs485_port`、`rtc_time_source`、`record_store_demo`、`mqtt_tls` 的默认 Core 2 环境均构建通过。MINIMAL/OFFLINE/LOCAL 的上述 basic 构建通过最终 ELF 符号裁剪检查；Core 3 受控 IOT 保留真实日期验证符号。架构、安全及发布内容检查通过。Core 3 构建仅出现工具链的串行 LTRANS 提示，不能解释为性能或硬件验证结果。

## 保留的边界与待验证项

- WiFi 的持续 STA 重连、DHCP 等待和安全启动保护，以及存储锁、写前校验和安全余量没有证据表明需要削弱或移植 ESP8266 策略，本轮保留。可选模块启动失败仍通过模块状态、错误日志和 facade 的最后错误定位，没有新增全局模块注册框架。
- 断连前未获 PUBACK 的出站消息仍是“送达不确定”，底层有界 outbox 可能重发；没有实现第二套持久队列，也没有承诺 exactly-once。平台/应用需使用稳定业务标识与去重。
- OTA 暂停是异步请求，尚未实测 TLS 内存释放时序；Web 写预算及 OTA 无进展检查不能抢占底层阻塞调用。真实慢网、异常断电、长时联网、存储写满/掉电、卡死复位和 OTA 恢复需真实设备验证。
- Core 2 看门狗不再把系统默认 5 秒改为 8 秒，产品升级需核对最长同步操作。未授权烧录、OTA、复位或接触真实执行器，本轮均未执行。
- 受控 TLS 包目前只验证 ESP32 / Core 3；没有把源码版本锁定外推成 ESP32-S3、ESP32-C3 或 Core 2 的安全 TLS 产物验证。跨版本和芯片的其余组合属于后续组合验收范围。

Record Store 控制文件仍为 128B，记录槽位仍为 payload + 24B；本机 `sizeof(Store)` 从 1,992B 到 2,016B（+24B，不等同芯片运行峰值）。没有增加持久 MQTT 队列或后台任务。容器格式 2 的升级影响见 [接入与升级](13_integration_and_upgrade.md)。

验证顺序以 ESP32 为主：普通能力使用当前默认 Core 2.0.16，需要完整 TLS 校验时使用已验证的受控 Core 3.3.8。S3/C3 的受控 TLS 构建与其他未覆盖组合统一后置到兼容性验收；现有通过结果复用，不能据此宣称所有组合均已通过。

维护检查点使用现有登记表，不新增常驻缓冲或任务。原生 Storage 测试覆盖 OTA 写暂停前检查点成功、失败仍允许暂停/恢复、失败后旧水位恢复，以及无变化不重复写。正常重启/deep sleep 由 facade 调用同一内部入口；未声称真实掉电或板端维护验证完成。

维护检查点收尾另构建 `full_demo` 的 ESP32/Core 2 LOCAL + Record Store 组合，RAM 58,508B / Flash 1,021,917B；记录示例上述 ESP32/Core 2 数字已更新。S3/C3、Core 3 的记录示例数字保留前一批构建证据，本次未重新测量；不能作为修改后示例的精确体积。架构、安全、发布hygiene检查通过。由当前Git管理文件构造干净打包输入，包180项且源码/示例一致，不包含本地经验碎片、构建缓存或工具链产物；临时包已删除。

配置失败与状态读取收尾：`clearSystemConfig()` 复用既有按key删除机制，NVS失败不取消deferred hostname，同namespace其他字段保留。新增回归在修复前复现pending数量从1变0，修复后Config原生23项通过。RecordStore容量读取由3次FS查询合为1次新鲜快照，不新增缓存；正常值、已用超过总量、查询失败输出清零和只读无写入由32项记录原生测试覆盖。主用ESP32/Core2的basic LOCAL与记录示例增量构建通过，表中对应数字已更新；查询次数减少不是实测延迟降低或运行堆峰值证据。其他既有构建数字未重测。
