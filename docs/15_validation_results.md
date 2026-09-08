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

## 网络恢复与 LOCAL 路径审查结论

| 场景 | 当前实现与判断 | 验证边界 |
| --- | --- | --- |
| 路由器离线、断联、DHCP超时 | WiFi.handle轮询状态、独立DHCP等待和STA退避，普通失败不转AP；初始化危险复位保护独立存在 | 没有照搬ESP8266的radio重置阈值；ESP32相同底层卡态未复现，不能把Broker故障直接解释为radio故障 |
| MQTT持续建连失败 | Base关闭SDK自动重连，以自身退避发起重连；两Core均设置10秒网络超时；TLS连接在SDK MQTT任务执行 | 不把10秒配置当严格端到端截止；不移植ESP8266同步建连的loop顺序作为ESP32优化 |
| Web/TLS资源叠加 | 保留有界收发、outbox、任务栈与TLS保护；OTA通过facade发出MQTT暂停 | 异步断开不能证明TLS内存已释放；正常Web与握手峰值仍需目标实测，未凭空添加堆阈值/全局调度框架 |
| mDNS初始化失败 | 新增5秒失败重试间隔，成功后幂等；只有成功才登记HTTP服务 | `test_mdns_retry.py`直接编译生产模块，覆盖持续失败、恢复、重复begin、显式stop与millis回绕；SDK单次阻塞和板端发现待验 |
| LOCAL配置与维护 | hostname清理失败保留deferred；OTA/重启/休眠前已登记Store检查点失败可诊断但不阻断恢复 | 原生Config、Store和既有OTA生命周期证据复用；真实Flash故障与复位仍待验 |
| 页面/存储状态容量 | RecordStore已将3次容量查询合为1次；Storage汇总再复用同一容量快照计算可写空间，失败不采信部分输出 | 两Store测试中总查询4次降为3次，另2次来自Store本身状态；未增加跨请求缓存，无假定的耗时百分比 |
| 日志、诊断与可选模块 | WebLogs读取落盘日志；FileLog故障停写、分级限频；RTC/RS485/Conditions按硬件与能力宏显式启用 | 不把ESP8266专用Journal或业务诊断分类搬入Base；不为“完善”新增业务框架 |

本批Record原生32项通过，ESP32/Core2 LOCAL+RecordStore集成最终静态RAM 58,516B、Flash 1,021,989B。此前该组合为58,508B/1,021,917B；新增重试状态有成本，容量查询优化不以减少保护内存为目标。未重建TLS工具链，未重新跑非主用矩阵。修正了Core兼容文档中与当前轮询实现不符的旧事件名称说明。

仍未证明的运行指标：TLS握手最小空闲堆/最大连续块、MQTT栈低水位，握手时LOCAL响应时延，OTA启动内存释放顺序，断网/错误时间恢复，以及长时间写入、重启后的资源趋势。应在指定ESP32设备上用当前固定固件、同一场景、改动前后可比条件集中测量；不能把本文代码审查和构建体积替代这些结果。平台SDK的序号编码/ACK映射、其他芯片/Core产物也不在本批完成声明内。

## 2026-09-09 ESP32 实机 Web SDK 适配修复

目标为用户新接入并授权烧录的实验 ESP32-D0WD-V3 rev3.1 / 4MB Flash。测试采用 IOT Profile、Core 3.3.8 和受控证书日期校验 SDK。烧录前完整备份 Flash；保留原分区表、NVS、文件系统，实际 OTA 槽为原板 1.25MiB，而非本库推荐分区表的 1.5MiB。此处不证明推荐分区布局或 OTA 已实测。

修复前，HTTP 请求导致 `StoreProhibited` 重启，故障地址 `0x7c`；同一固件 ELF 回溯定位 `WebContext.cpp` 请求头清理。Core 3.3.8 的 `_currentHeaders` 已是链表，原实现按数组下标访问；Core 2.0.16 则仍为数组。内部按实际头类型选择遍历方式，清空值而保留头名称与链路，无新增常驻缓冲、任务或公共 API。

修复后同一板 50 秒采样窗口内，10 次带认证状态 API 请求全部 HTTP 200（48–122ms），5 次无认证请求全部 HTTP 401；无 panic，uptime 连续增长。MQTT 当时处于 DNS 失败退避，不能将这些响应时间表述为 TLS 握手并发指标。实验板使用路由器 DNS 时，Broker 解析失败；主机直接查询同一 DNS 也返回 NXDOMAIN，而公共 DNS 返回有效记录，属于本次环境条件。

定向 `scripts/test_web_header_reset.py` 覆盖数组、链表、重复清理、空集合及保留名称/链接；Web 原生 21 项通过。Core 2 ESP32 LOCAL 增量构建通过（静态 RAM 57,060B / Flash 983,913B），受控 Core 3 实测探针构建通过；架构和安全边界检查通过。探针和认证配置仅保存在本地忽略目录，未进入发布源码。未重新运行 S3/C3 或全部 Core 矩阵；短窗口测试不能替代长期稳定性验收。

扩大请求量后又确认两处同路径适配缺口：新版响应头链表未随本库自有请求生命周期释放，80次请求中出现56次客户端HTTPException，空闲堆从149,520B降到121,184B；已在结束客户端时释放响应头（包括拒绝请求），旧版String响应头同步清空。新版NetworkClient将socket与Stream超时分开且使用毫秒，已按实际SDK方法设置两者，旧版保留秒接口。定向回归补充100轮响应节点存活数归零和两类客户端的单位/双超时检查。

最终版第一轮50秒窗口内100次状态API请求全部符合预期（66次认证HTTP200、34次无认证HTTP401，耗时34–155ms）；未出现panic，最后10次空闲堆采样均为146,272B。窗口从等待时间同步跨入DNS退避，不能直接用首尾堆值计算泄漏；连续块最小106,484B，loop单次采样最大63ms。仍未进入TLS握手，MQTT连接与PUBACK均为0。临时只在实验板设置公共DNS成功，但板端解析仍失败，未改Base/路由器默认DNS策略，也未跳过证书验证。

暖机复测另100次状态请求同样全部通过（66次200、34次401）；另检查status/system/wifi/logs/fs五个只读页面，均HTTP200且完整返回HTML，耗时96–509ms，未做浏览器视觉或修改配置操作。暖机窗口末尾空闲堆145,676B、最大连续块110,580B；与窗口初始146,256B相差580B，期间包含首次读取上述页面和MQTT退避，不能归因为零泄漏，也未复现此前80次请求下降28,336B及响应头超限的现象。长期趋势仍待后续测量。最终探针.bin为1,297,456B，SHA256 `d1920ea23fedb73ef4f1e1564a6ca8d08366ac1e4aacda5f13928e123e0d0068`，板端写入校验通过；静态RAM62,016B/Flash1,297,048B。两种Core受影响构建、21项Web原生测试、定向SDK布局/超时测试与边界检查通过；干净输入打包184项，含新增内部头文件，不含私密配置和过程文件，临时包已删除。


## 2026-09-09 ESP32 隔离 TLS、MQTT 与 OTA 实测

沿用同一 ESP32-D0WD-V3 rev3.1 / 4MB 实验板、原 1.25MiB 双 OTA 槽和受控 Core 3.3.8 SDK。独立测试固件使用本机 IP、临时 P-256 CA/服务端证书和唯一非 retained 测试主题，保持证书链、名称和日期校验开启。测试端点仅实现本次所需的 MQTT 3.1.1 CONNECT、SUBSCRIBE、QoS1 双向 PUBLISH/PUBACK、PING 与断开，不等同于生产 Broker、云端 DNS或 IoT 平台协议验收。私密认证、证书私钥和原始日志均留在本地忽略目录。

实机发现并修复：SDK 在 X.509 验证失败时可能返回 TLS 错误 `0x2700` 而 certificate flags 为0。原分类器只凭非零 flags 识别终止证书错误，因而错误地继续普通TLS退避。现在同时识别上游 `MBEDTLS_ERR_X509_CERT_VERIFY_FAILED` 的正负表示，遵循既有 `ERROR_TLS_CERTIFICATE` / `CONNECTION_REJECTED` 契约；不推测缺失的具体标志，不增加证书常驻缓存，不修改SDK或关闭验证。

| 最终实机证书场景 | 结果 |
| --- | --- |
| 有效证书 | TLS连接、订阅、QoS1发布确认与入站消息确认成功 |
| 过期 / 尚未生效 / 名称不匹配 / 不可信签发者 | 四类均拒绝，进入CONNECTION_REJECTED；各窗口最后10次采样的尝试次数保持不变，没有成功连接增长 |
| 每次换回有效证书并显式requestReconnect | 四次均恢复连接与消息收发；未将仅换证书当作自动解除终止拒绝的条件 |

最终证书矩阵持续约215秒，213次一秒遥测；同时361次认证状态API请求全部HTTP200，P95 148ms、最大1107ms。累计成功连接6次、发布PUBACK计数5、入站probe回调6次；人为中途断开可能使确认来不及被应用处理，不能把连接数等同完整消息事务数。内部8-bit堆的启动以来低水位68,652B，采样到的最小连续块73,716B；后者是一秒采样值，不能当作瞬时最低连续块。最大handle耗时952ms，慢点集中在握手/恢复附近，尚需定位，不能宣称网络异步任务已保证loop低延迟。指标只覆盖此证书链和负载，不是减小重连缓冲、MQTT栈或TLS预算的依据。

OTA中止测试先在已连接MQTT时直接startUpload并abort：开始前内部空闲堆103,952B，Update.begin之后99,340B，中止后恢复103,952B。MQTT状态已标记暂停，但TLS内存尚未释放，不能把异步断开提前计入可用预算。再经认证raw入口发送真实镜像前16,384B后关闭连接，OTA明确FAILED并保留中止原因；目标为app1，运行及启动仍为app0，MQTT重新连接和PUBACK随后恢复。该探针未开启RecordStore，不能代替记录检查点的实机验收。

首次完整raw上传及SHA256核对成功（1,299,088B，15.246秒）；实验固件未调用markCurrentValid，30秒后按保护契约触发重启并回到app0。SDK当时报告没有可回滚的已确认槽，随后bootloader选择app0；这是该板原始OTA元数据条件，不能称为“两槽均已确认”的标准回滚验证。该测试缺失的应用确认已在实验固件中补齐，基础库不替应用自动确认。

补齐实验应用启动自检确认后的完整OTA再次通过：1,299,888B，SHA256 `93f09187dc7d24928e558f6cc545edf3cdb27aa0601c03d11089a8cf4ec64b7d`；HTTP200，返回计算哈希一致，主机测10.950秒。新镜像启动输出mark-valid成功，超过30秒确认期限后API仍显示运行及启动槽app1、状态valid、无需确认，TLS/MQTT已恢复。最终探针只增加实验遥测和启动自检，库代码与上述证书矩阵相同。确认当前固定SDK源码的唯一MQTT任务名称后读取其真实任务句柄，46次有效栈采样最小剩余3,040B（配置6,144B）；该ESP32端口的StackType_t为1字节，不把loop任务栈冒充MQTT栈，不据此缩减预算。

最后仅在实验板执行WiFi.disconnect(false,false)，保留WiFi凭据、保持AP不变；采样从WAITING_FOR_WIFI到MQTT重新CONNECTED约9.146秒（1秒采样估计），成功连接和PUBACK均从1增到2。40秒窗口内Web有55次HTTP200、断网期间3次连接错误，恢复后继续成功，无panic。MQTT任务最小剩余栈仍为3,040B。此为主动断联的一次恢复，不代表路由器长期离线、弱网或反复掉电验收。

本批20项MQTT原生及1项安全默认验证通过；Core2 ESP32 IOT与受控Core3实测固件增量构建通过，架构/安全/发布hygiene检查通过。S3/C3与其他Core矩阵继续后置，不重复构建TLS工具链。完整实测仍未覆盖云端DNS集成、近1秒loop延迟根因、长稳/突发负载、真实RecordStore维护与掉电、已确认双槽的标准rollback条件。不能把本次证书/OTA/恢复闭合扩大成整个Esp32Base完成。

最终构建记录：basic ESP32/Core2 IOT静态RAM59,240B、Flash996,485B；确认版实机探针静态RAM62,032B、Flash1,299,480B（与私有证书/遥测相关，不作基础库前后差值）。干净Git输入打包185项，MQTT修复源码在包中，私密配置、缓存和过程文件排除，临时包已删除。
