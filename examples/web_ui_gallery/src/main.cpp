#include <Arduino.h>
#include <Esp32Base.h>
#include <WiFi.h>

#ifndef ESP32BASE_WEB_UI_GALLERY_SELFTEST
#define ESP32BASE_WEB_UI_GALLERY_SELFTEST 0
#endif

const Esp32BaseWeb::ResultNotice GALLERY_RESULTS[] = {
    {"saved", "1", Esp32BaseWeb::UI_OK, "保存成功", "页面已通过 POST -> 303 -> GET 返回。"},
    {"done", "1", Esp32BaseWeb::UI_OK, "操作已提交", "这是一次性命令的完成反馈。"},
    {"blocked", "1", Esp32BaseWeb::UI_WARN, "暂不能执行", "设备状态不满足条件时，应说明原因和下一步。"}
};

#if ESP32BASE_WEB_UI_GALLERY_SELFTEST
static bool g_selfTestDone = false;

size_t advanceMatch(const char* pattern, size_t patternLen, size_t matched, char c) {
    while (matched > 0 && c != pattern[matched]) {
        size_t next = matched - 1;
        while (next > 0 && strncmp(pattern, pattern + matched - next, next) != 0) {
            --next;
        }
        matched = next;
    }
    if (c == pattern[matched]) {
        ++matched;
        if (matched > patternLen) {
            matched = patternLen;
        }
    }
    return matched;
}

bool selfTestRequest(const char* method, const char* path, const char* body, bool auth, int expectedCode, const char* mustContain) {
    WiFiClient client;
    const IPAddress targetIp = Esp32BaseWiFi::state() == Esp32BaseWiFi::CONFIG_PORTAL ? WiFi.softAPIP() : WiFi.localIP();
    if (!client.connect(targetIp, 80)) {
        ESP32BASE_LOG_E("selftest", "%s %s connect_failed", method, path);
        return false;
    }
    client.print(method);
    client.print(" ");
    client.print(path);
    client.print(" HTTP/1.1\r\nHost: ");
    client.print(targetIp);
    client.print("\r\nConnection: close\r\n");
    if (auth) {
        client.print("Authorization: Basic YWRtaW46YWRtaW4=\r\n");
    }
    const size_t bodyLen = body ? strlen(body) : 0;
    if (bodyLen > 0) {
        client.print("Content-Type: application/x-www-form-urlencoded\r\nContent-Length: ");
        client.print(bodyLen);
        client.print("\r\n");
    }
    client.print("\r\n");
    if (bodyLen > 0) {
        client.print(body);
    }

    char statusLine[96];
    size_t statusUsed = 0;
    size_t matchUsed = 0;
    const size_t matchLen = mustContain ? strlen(mustContain) : 0;
    bool contains = !mustContain || matchLen == 0;
    bool firstLineDone = false;
    const uint32_t deadline = millis() + 5000UL;
    while (millis() < deadline && (client.connected() || client.available())) {
        while (client.available()) {
            const char c = static_cast<char>(client.read());
            if (!firstLineDone) {
                if (c == '\n') {
                    firstLineDone = true;
                } else if (c != '\r' && statusUsed + 1 < sizeof(statusLine)) {
                    statusLine[statusUsed++] = c;
                }
            }
            if (!contains && mustContain) {
                matchUsed = advanceMatch(mustContain, matchLen, matchUsed, c);
                if (matchUsed == matchLen) {
                    contains = true;
                }
            }
        }
        Esp32Base::handle();
        delay(1);
    }
    statusLine[statusUsed] = '\0';
    client.stop();

    int code = 0;
    sscanf(statusLine, "HTTP/%*s %d", &code);
    const bool ok = code == expectedCode && contains;
    ESP32BASE_LOG_I("selftest", "%s %s code=%d expected=%d contains=%s result=%s",
                    method, path, code, expectedCode, mustContain ? mustContain : "-",
                    ok ? "pass" : "fail");
    return ok;
}

void runSelfTest() {
    static uint32_t lastWaitLogMs = 0;
    if (g_selfTestDone) {
        return;
    }
    if (!Esp32BaseWeb::isReady()) {
        const uint32_t now = millis();
        if (now - lastWaitLogMs > 5000UL) {
            lastWaitLogMs = now;
            ESP32BASE_LOG_I("selftest", "waiting wifi=%s web=%s state=%s",
                            Esp32BaseWiFi::isConnected() ? "yes" : "no",
                            Esp32BaseWeb::isReady() ? "yes" : "no",
                            Esp32BaseWiFi::stateName());
        }
        return;
    }
    g_selfTestDone = true;
    uint8_t pass = 0;
    uint8_t total = 0;
#define RUN_SELFTEST(method, path, body, auth, code, contains) do { ++total; if (selfTestRequest(method, path, body, auth, code, contains)) ++pass; } while (0)
    RUN_SELFTEST("GET", "/", nullptr, true, 302, "Location: /ui-status");
    RUN_SELFTEST("GET", "/ui-status", nullptr, true, 200, "状态概览");
    RUN_SELFTEST("GET", "/ui-stats", nullptr, true, 200, "统计摘要");
    RUN_SELFTEST("GET", "/ui-records?page=2", nullptr, true, 200, "共 128 条 / 7 页");
    RUN_SELFTEST("GET", "/ui-records?page=2", nullptr, true, 200, "range=24h&amp;type=all&amp;page=1");
    RUN_SELFTEST("GET", "/ui-config?saved=1", nullptr, true, 200, "保存成功");
    RUN_SELFTEST("GET", "/ui-action", nullptr, true, 200, "操作命令");
    RUN_SELFTEST("POST", "/ui-action/run", "run=1", true, 303, "Location: /ui-action?done=1");
    RUN_SELFTEST("GET", "/ui-flow?saved=1", nullptr, true, 200, "流程向导");
    RUN_SELFTEST("GET", "/ui-maintenance", nullptr, true, 200, "诊断维护");
    RUN_SELFTEST("GET", "/ui-access", nullptr, false, 200, "访问控制");
    RUN_SELFTEST("GET", "/ui-confirm", nullptr, true, 200, "确认保护");
    RUN_SELFTEST("POST", "/ui-confirm/run", "confirm=1", true, 303, "Location: /ui-confirm?done=1");
    RUN_SELFTEST("GET", "/ui-empty", nullptr, true, 200, "空状态");
    RUN_SELFTEST("GET", "/ui-form", nullptr, true, 200, "多字段表单");
    RUN_SELFTEST("POST", "/ui-form/save", "name=gallery&limit=30", true, 303, "Location: /ui-form?saved=1");
#undef RUN_SELFTEST
    ESP32BASE_LOG_I("selftest", "summary pass=%u total=%u", static_cast<unsigned>(pass), static_cast<unsigned>(total));
}
#endif

void handleHeadExtra() {
    Esp32BaseWeb::sendChunk("<style id='gallery-head-extra'>.swatch{display:inline-flex;gap:6px;align-items:center}.swatch i{display:inline-block;width:18px;height:18px;border-radius:5px;border:1px solid var(--eb-line)}.formgrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.formgrid p{margin:0}@media(max-width:760px){.formgrid{grid-template-columns:1fr}}</style>");
}

void handlePostRedirect(const char* location) {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    if (!Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)) {
        Esp32BaseWeb::sendText(405, "method not allowed");
        return;
    }
    Esp32BaseWeb::redirectSeeOther(location);
}

void handleActionRun() {
    handlePostRedirect("/ui-action?done=1");
}

void handleConfirmRun() {
    handlePostRedirect("/ui-confirm?done=1");
}

void handleFormSave() {
    handlePostRedirect("/ui-form?saved=1");
}

void sendGalleryResults() {
    Esp32BaseWeb::sendResultNotice(GALLERY_RESULTS, 3);
}

void handleStatusPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Status");
    Esp32BaseWeb::sendPageTitle("状态概览", "设备首页、局部总览和实时状态页的推荐结构。");
    sendGalleryResults();
    Esp32BaseWeb::beginPanel("当前状态");
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "设备正常", "当前空闲，下一计划 18:30。");
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("当前状态", "空闲", "可执行");
    Esp32BaseWeb::sendMetric("下一计划", "18:30");
    Esp32BaseWeb::sendMetric("可用通道", "4 路");
    Esp32BaseWeb::sendMetric("异常", "0");
    Esp32BaseWeb::endMetricGrid();
    Esp32BaseWeb::sendInfoRowCompactLink("最近记录", "只放和判断状态有关的最近事件。", "2 条", "/ui-records", "查看");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::beginPanel("页面模板目录");
    Esp32BaseWeb::sendInfoRowCompactLink("状态与统计", "设备首页和轻量统计摘要，避免引入图表库。", nullptr, "/ui-stats", "查看");
    Esp32BaseWeb::sendInfoRowCompactLink("记录与分页", "筛选、表头、空状态和页码型分页。", nullptr, "/ui-records", "查看");
    Esp32BaseWeb::sendInfoRowCompactLink("配置与表单", "紧凑配置列表、行内动作和多字段独立编辑页。", nullptr, "/ui-config", "查看");
    Esp32BaseWeb::sendInfoRowCompactLink("命令与确认", "一次性动作、危险确认和 PRG 防重复提交。", nullptr, "/ui-action", "查看");
    Esp32BaseWeb::sendInfoRowCompactLink("维护与权限", "诊断维护、访问受限和空状态。", nullptr, "/ui-maintenance", "查看");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleStatsPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Stats");
    Esp32BaseWeb::sendPageTitle("统计摘要", "不用图表库，用数字、表格和轻量标签表达摘要。");
    Esp32BaseWeb::beginPanel("本月摘要");
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("次数", "128");
    Esp32BaseWeb::sendMetric("用量", "32.4L");
    Esp32BaseWeb::sendMetric("成功率", "98%");
    Esp32BaseWeb::sendMetric("较上周", "+6%");
    Esp32BaseWeb::endMetricGrid();
    Esp32BaseWeb::sendChunk("<table class='kv'><tr><th>分组</th><td><span class='tag ok'>东区 42%</span> <span class='tag info'>西区 31%</span> <span class='tag'>其他 27%</span></td></tr><tr><th>趋势</th><td>最近 7 天稳定，无异常尖峰。</td></tr></table>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleRecordsPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    char pageText[12] = "1";
    Esp32BaseWeb::getParam("page", pageText, sizeof(pageText));
    uint32_t page = static_cast<uint32_t>(strtoul(pageText, nullptr, 10));
    if (page == 0) {
        page = 1;
    }
    Esp32BaseWeb::sendHeader("UI Records");
    Esp32BaseWeb::sendPageTitle("记录列表", "筛选、表头、空状态和分页的基准样式。");
    Esp32BaseWeb::beginPanel("最近记录");
    Esp32BaseWeb::sendChunk("<div class='actions'><span class='tag info'>最近 24 小时</span><span class='tag'>全部类型</span><span class='tag'>每页 20 条</span></div>");
    Esp32BaseWeb::sendChunk("<table class='kv'><tr><th>时间</th><th>类型</th><th>结果</th></tr><tr><td>18:30</td><td>计划执行</td><td><span class='tag ok'>完成</span></td></tr><tr><td>16:10</td><td>手动执行</td><td><span class='tag ok'>完成</span></td></tr><tr><td>14:02</td><td>保护触发</td><td><span class='tag warn'>已跳过</span></td></tr></table>");
    Esp32BaseWeb::Pagination pagination = {"/ui-records?range=24h", "type=all", page, 20, 128};
    Esp32BaseWeb::sendPagination(pagination);
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleConfigPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Config");
    Esp32BaseWeb::sendPageTitle("配置编辑", "简单字段行内改，多字段对象进入独立编辑页。");
    sendGalleryResults();
    Esp32BaseWeb::beginPanel("紧凑配置列表");
    Esp32BaseWeb::sendInfoRowCompactLink("第 1 路名称", "用于页面和记录展示，不影响实际控制。", "花坛", "/ui-form", "展开修改");
    Esp32BaseWeb::sendInfoRowCompactLink("默认计划", "包含时间、通道、执行天数和目标量。", "3 条", "/ui-flow", "进入编辑页");
    Esp32BaseWeb::sendInfoRowCompactLink("恢复出厂", "高风险操作必须进入确认保护页。", nullptr, "/ui-confirm", "确认页", Esp32BaseWeb::UI_DANGER);
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleActionPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Action");
    Esp32BaseWeb::sendPageTitle("操作命令", "一次性动作与长期配置分离，提交后必须回到 GET 页面。");
    sendGalleryResults();
    Esp32BaseWeb::beginPanel("立即执行");
    Esp32BaseWeb::sendInfoRowCompactForm("可执行", "设备空闲、时间可信、无严重异常。", nullptr, "/ui-action/run", "开始", "run", "1");
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "拒绝执行状态", "条件不足时直接解释原因，不让用户猜。");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleFlowPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Flow");
    Esp32BaseWeb::sendPageTitle("流程向导", "用于校准、首次设置和复杂维护。");
    sendGalleryResults();
    Esp32BaseWeb::beginPanel("三步流程");
    Esp32BaseWeb::sendInfoRowCompact("1. 依据", "展示旧值、数据来源和是否允许继续。", "通过");
    Esp32BaseWeb::sendInfoRowCompactLink("2. 实测", "录入真实测量结果，不塞进普通配置表。", nullptr, "/ui-form", "继续");
    Esp32BaseWeb::sendInfoRowCompactLink("3. 核对保存", "展示旧值、新值、变化幅度和影响范围。", nullptr, "/ui-flow?saved=1", "保存");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleMaintenancePage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Maintenance");
    Esp32BaseWeb::sendPageTitle("诊断维护", "只读优先，危险动作进入确认保护页。");
    Esp32BaseWeb::beginPanel("系统诊断");
    Esp32BaseWeb::sendInfoRowCompact("WiFi", "连接状态、RSSI、IP。", "正常");
    Esp32BaseWeb::sendInfoRowCompactLink("维护任务", "导出、扫描、重启等长任务显示状态和下一步。", "空闲", "/esp32base/tools", "查看");
    Esp32BaseWeb::sendInfoRowCompactLink("访问控制", "登录、权限不足、会话失效和只读受限状态。", nullptr, "/ui-access", "查看");
    Esp32BaseWeb::sendInfoRowCompactLink("空状态", "列表、记录或配置项暂不存在时的基准表达。", nullptr, "/ui-empty", "查看");
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "原始数据受控", "限制长度，可复制或导出，不做无限滚动调试平台。");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleAccessPage() {
    Esp32BaseWeb::sendHeader("UI Access");
    Esp32BaseWeb::sendPageTitle("访问控制", "覆盖登录、权限不足、会话失效和只读受限状态。");
    Esp32BaseWeb::beginPanel("受限状态");
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "需要登录", "登录后可以继续执行配置和维护操作。");
    Esp32BaseWeb::sendInfoRowCompactLink("只读访问", "无权限时展示原因和可继续查看的内容。", "允许", "/esp32base", "返回状态");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleConfirmPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Confirm");
    Esp32BaseWeb::sendPageTitle("确认保护", "高风险操作必须隔离展示影响范围和确认动作。");
    sendGalleryResults();
    Esp32BaseWeb::beginPanel("高风险操作");
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "会影响设备状态", "真实项目中应展示影响范围、保留数据和恢复方式。");
    Esp32BaseWeb::sendInfoRowCompactForm("恢复出厂", "示例只演示页面流程，不执行真实清理。", "保留记录", "/ui-confirm/run", "确认执行", "confirm", "1", Esp32BaseWeb::UI_DANGER);
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleEmptyPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Empty");
    Esp32BaseWeb::sendPageTitle("空状态", "列表、记录或配置项暂不存在时的基准表达。");
    Esp32BaseWeb::beginPanel("暂无记录");
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_INFO, "没有匹配记录", "调整筛选条件，或等待设备产生新的记录。");
    Esp32BaseWeb::sendChunk("<table class='kv'><tr><th>时间</th><th>类型</th><th>结果</th></tr><tr><td colspan='3'><span class='muted'>暂无数据</span></td></tr></table>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleFormPage() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Form");
    Esp32BaseWeb::sendPageTitle("多字段表单", "多字段对象默认进入独立编辑页，保存后返回展示页。");
    sendGalleryResults();
    Esp32BaseWeb::beginPanel("计划编辑");
    Esp32BaseWeb::sendChunk("<form method='post' action='/ui-form/save' onsubmit='return once(this)'><div class='formgrid'><p><label>名称</label><input name='name' maxlength='16' value='花坛计划'></p><p><label>目标量</label><input type='number' name='limit' min='1' max='120' value='30'></p><p><label>模式</label><select name='mode'><option>auto</option><option>manual</option></select></p><p><label>说明</label><input name='note' maxlength='24' value='工作日傍晚执行'></p></div><div class='actions'><input type='submit' value='保存'><input type='button' value='取消' onclick=\"location.href='/ui-config'\"></div></form>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void setup() {
    Esp32Base::setFirmwareInfo("web-ui-gallery", "1.0.0");
    Esp32BaseWeb::setDefaultAuth("admin", "admin");
    Esp32BaseWeb::setDeviceName("UI 样式");
    Esp32BaseWeb::setHomePath("/ui-status");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_APP);
    Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_BOTTOM);
    Esp32BaseWeb::setHeadExtraCallback(handleHeadExtra);
    Esp32BaseWeb::addPage("/ui-status", "总览", handleStatusPage);
    Esp32BaseWeb::addPage("/ui-records", "记录", handleRecordsPage);
    Esp32BaseWeb::addPage("/ui-config", "配置", handleConfigPage);
    Esp32BaseWeb::addPage("/ui-action", "命令", handleActionPage);
    Esp32BaseWeb::addPage("/ui-form", "表单", handleFormPage);
    Esp32BaseWeb::addRoute("/ui-stats", Esp32BaseWeb::METHOD_GET, handleStatsPage);
    Esp32BaseWeb::addRoute("/ui-flow", Esp32BaseWeb::METHOD_GET, handleFlowPage);
    Esp32BaseWeb::addRoute("/ui-maintenance", Esp32BaseWeb::METHOD_GET, handleMaintenancePage);
    Esp32BaseWeb::addRoute("/ui-access", Esp32BaseWeb::METHOD_GET, handleAccessPage);
    Esp32BaseWeb::addRoute("/ui-confirm", Esp32BaseWeb::METHOD_GET, handleConfirmPage);
    Esp32BaseWeb::addRoute("/ui-empty", Esp32BaseWeb::METHOD_GET, handleEmptyPage);
    Esp32BaseWeb::addRoute("/ui-action/run", Esp32BaseWeb::METHOD_POST, handleActionRun);
    Esp32BaseWeb::addRoute("/ui-confirm/run", Esp32BaseWeb::METHOD_POST, handleConfirmRun);
    Esp32BaseWeb::addRoute("/ui-form/save", Esp32BaseWeb::METHOD_POST, handleFormSave);
    Esp32Base::begin();
    ESP32BASE_LOG_I("example", "web ui gallery ready");
}

void loop() {
    Esp32Base::handle();
#if ESP32BASE_WEB_UI_GALLERY_SELFTEST
    runSelfTest();
#endif
    delay(10);
}
