#include <Arduino.h>
#include <Esp32Base.h>

const Esp32BaseWeb::ResultNotice GALLERY_RESULTS[] = {
    {"saved", "1", Esp32BaseWeb::UI_OK, "保存成功", "页面已通过 POST -> 303 -> GET 返回。"},
    {"done", "1", Esp32BaseWeb::UI_OK, "操作已提交", "这是一次性命令的完成反馈。"},
    {"blocked", "1", Esp32BaseWeb::UI_WARN, "暂不能执行", "设备状态不满足条件时，应说明原因和下一步。"}
};

static const char* GALLERY_NS = "ui_gallery";
static const char* GALLERY_KEY_TITLE = "title";
static const char* GALLERY_KEY_INTERVAL = "interval";
static const char* GALLERY_KEY_READONLY = "readonly";

void handleHeadExtra() {
    Esp32BaseWeb::sendChunk("<style id='gallery-head-extra'>.swatch{display:inline-flex;gap:6px;align-items:center}.swatch i{display:inline-block;width:18px;height:18px;border-radius:5px;border:1px solid var(--eb-line)}.inlineedit{border:1px solid #b9e3d9;background:#f3fbf8;border-radius:8px;padding:10px;margin:0 0 10px}.inlineedit form{display:grid;grid-template-columns:1fr auto auto;gap:8px;align-items:end}.inlineedit input{margin:0}@media(max-width:760px){.inlineedit form{grid-template-columns:1fr}.inlineedit .actions{margin-top:0}}</style>");
}

void handlePostRedirect(const char* location) {
    if (!Esp32BaseWeb::checkPostAllowed("gallery_post")) {
        return;
    }
    Esp32BaseWeb::redirectSeeOther(location);
}

void handleActionRun() {
    handlePostRedirect("/ui-action?done=1");
}

void handleConfirmRun() {
    handlePostRedirect("/ui-config/confirm?done=1");
}

void handleFormSave() {
    handlePostRedirect("/ui-form?saved=1");
}

void htmlEscapeTo(const char* in, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    size_t used = 0;
    out[0] = '\0';
    const char* text = in ? in : "";
    for (const char* p = text; *p && used + 1 < len; ++p) {
        const char* repl = nullptr;
        switch (*p) {
            case '&': repl = "&amp;"; break;
            case '<': repl = "&lt;"; break;
            case '>': repl = "&gt;"; break;
            case '"': repl = "&quot;"; break;
            case '\'': repl = "&#39;"; break;
            default: break;
        }
        if (repl) {
            const size_t replLen = strlen(repl);
            if (used + replLen >= len) {
                break;
            }
            memcpy(out + used, repl, replLen);
            used += replLen;
        } else {
            out[used++] = *p;
        }
    }
    out[used] = '\0';
}

void buildChannelNameRowHtml(const char* value, char* out, size_t len) {
    char escaped[64];
    htmlEscapeTo(value && value[0] ? value : "花坛", escaped, sizeof(escaped));
    snprintf(out, len,
             "<div id='row-channel-name'><div class='urow' data-eb-inline-edit><div><b>第 1 路名称</b><small>用于页面和记录展示，不影响实际控制。</small></div><div class='uactions'><span class='uvalue'>%s</span><button type='button' class='btnlink info' data-eb-inline-toggle='row-channel-name-edit'>编辑</button></div></div><div id='row-channel-name-edit' class='eb-inline-edit'><form method='post' data-eb-ajax action='/ui-config/name'><label>第 1 路名称</label><input name='name' value='%s' autocomplete='off'><div data-eb-error class='eb-inline-error'></div><div class='actions'><button type='button' class='secondary' data-eb-inline-close='row-channel-name-edit'>Cancel</button><input type='submit' value='Save'></div></form></div></div>",
             escaped,
             escaped);
}

void buildPlanRowHtml(const char* mode, const char* limit, char* out, size_t len) {
    char modeEsc[24];
    char limitEsc[16];
    htmlEscapeTo(mode && mode[0] ? mode : "auto", modeEsc, sizeof(modeEsc));
    htmlEscapeTo(limit && limit[0] ? limit : "30", limitEsc, sizeof(limitEsc));
    snprintf(out, len,
             "<div id='row-plan'><div class='urow'><div><b>默认计划</b><small>包含时间、通道、执行天数和目标量。</small></div><div class='uactions'><span class='uvalue'>%s / %s</span><button type='button' class='btnlink info' data-eb-dialog-open='dlg-plan'>快速编辑</button></div></div></div>",
             modeEsc,
             limitEsc);
}

void handleConfigNameSave() {
    if (!Esp32BaseWeb::checkPostAllowed("gallery_config_name")) {
        return;
    }
    char name[32] = "";
    Esp32BaseWeb::getParam("name", name, sizeof(name));
    if (!name[0]) {
        if (Esp32BaseWeb::isAjaxRequest()) {
            Esp32BaseWeb::sendAjaxError(400, "Name is required.");
        } else {
            Esp32BaseWeb::redirectSeeOther("/ui-config?blocked=1");
        }
        return;
    }
    if (Esp32BaseWeb::isAjaxRequest()) {
        char html[920];
        buildChannelNameRowHtml(name, html, sizeof(html));
        Esp32BaseWeb::sendAjaxReplace("row-channel-name", html, "保存成功");
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/ui-config?saved=1");
}

void handleConfigDialogSave() {
    if (!Esp32BaseWeb::checkPostAllowed("gallery_config_dialog")) {
        return;
    }
    char limit[16] = "";
    char mode[16] = "";
    Esp32BaseWeb::getParam("limit", limit, sizeof(limit));
    Esp32BaseWeb::getParam("mode", mode, sizeof(mode));
    const int value = atoi(limit);
    const bool validMode = strcmp(mode, "auto") == 0 || strcmp(mode, "manual") == 0;
    if (value < 1 || value > 120 || !validMode) {
        if (Esp32BaseWeb::isAjaxRequest()) {
            Esp32BaseWeb::sendAjaxError(400, "Plan values are invalid.");
        } else {
            Esp32BaseWeb::redirectSeeOther("/ui-config?blocked=1");
        }
        return;
    }
    if (Esp32BaseWeb::isAjaxRequest()) {
        char html[420];
        buildPlanRowHtml(mode, limit, html, sizeof(html));
        Esp32BaseWeb::sendAjaxReplace("row-plan", html, "保存成功");
        return;
    }
    Esp32BaseWeb::redirectSeeOther("/ui-config?saved=1");
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
    Esp32BaseWeb::beginPanel("页面示例目录");
    Esp32BaseWeb::sendInfoRowCompactLink("状态与统计", "设备首页和轻量统计摘要，避免引入图表库。", nullptr, "/ui-status/stats", "查看");
    Esp32BaseWeb::sendInfoRowCompactLink("记录与分页", "筛选、表头、空状态和页码型分页。", nullptr, "/ui-records", "查看");
    Esp32BaseWeb::sendInfoRowCompactLink("配置与表单", "紧凑配置列表、行内动作和多字段独立编辑页。", nullptr, "/ui-config", "查看");
    Esp32BaseWeb::sendInfoRowCompactLink("命令与确认", "一次性动作、危险确认和 PRG 防重复提交。", nullptr, "/ui-action", "查看");
    Esp32BaseWeb::sendInfoRowCompactLink("维护与权限", "诊断维护、访问受限和空状态。", nullptr, "/ui-status/maintenance", "查看");
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
    char range[12] = "24h";
    Esp32BaseWeb::getParam("range", range, sizeof(range));
    if (strcmp(range, "24h") != 0 && strcmp(range, "7d") != 0 && strcmp(range, "30d") != 0 && strcmp(range, "custom") != 0) {
        strlcpy(range, "24h", sizeof(range));
    }
    char type[12] = "all";
    Esp32BaseWeb::getParam("type", type, sizeof(type));
    if (strcmp(type, "all") != 0 && strcmp(type, "plan") != 0 && strcmp(type, "manual") != 0 && strcmp(type, "guard") != 0) {
        strlcpy(type, "all", sizeof(type));
    }
    char startTime[24] = "";
    char endTime[24] = "";
    Esp32BaseWeb::getParam("start", startTime, sizeof(startTime));
    Esp32BaseWeb::getParam("end", endTime, sizeof(endTime));
    auto sanitizeDateTime = [](char* s) {
        for (size_t i = 0; s[i]; ++i) {
            const char c = s[i];
            const bool ok = (c >= '0' && c <= '9') || c == '-' || c == ':' || c == 'T';
            if (!ok) {
                s[i] = '\0';
                break;
            }
        }
    };
    sanitizeDateTime(startTime);
    sanitizeDateTime(endTime);
    char pageText[12] = "1";
    Esp32BaseWeb::getParam("page", pageText, sizeof(pageText));
    uint32_t page = static_cast<uint32_t>(strtoul(pageText, nullptr, 10));
    if (page == 0) {
        page = 1;
    }
    char perText[12] = "10";
    Esp32BaseWeb::getParam("per", perText, sizeof(perText));
    uint32_t perPage = static_cast<uint32_t>(strtoul(perText, nullptr, 10));
    if (perPage != 10 && perPage != 15 && perPage != 20 && perPage != 30 && perPage != 50) {
        perPage = 10;
    }
    Esp32BaseWeb::sendHeader("UI Records");
    Esp32BaseWeb::sendPageTitle("记录列表", "筛选、表头、空状态和分页的基准样式。");
    Esp32BaseWeb::beginPanel("最近记录");
    const bool customRange = strcmp(range, "custom") == 0;
    Esp32BaseWeb::sendChunk("<form method='get' action='/ui-records' class='filterbar'><label>时间范围</label><select name='range' onchange='this.form.submit()'>");
    const char* rangeValues[] = {"24h", "7d", "30d", "custom"};
    const char* rangeLabels[] = {"最近 24 小时", "最近 7 天", "最近 30 天", "自定义"};
    for (uint8_t i = 0; i < 4; ++i) {
        Esp32BaseWeb::sendChunk("<option value='");
        Esp32BaseWeb::sendChunk(rangeValues[i]);
        Esp32BaseWeb::sendChunk(strcmp(range, rangeValues[i]) == 0 ? "' selected>" : "'>");
        Esp32BaseWeb::sendChunk(rangeLabels[i]);
        Esp32BaseWeb::sendChunk("</option>");
    }
    Esp32BaseWeb::sendChunk("</select>");
    if (customRange) {
        Esp32BaseWeb::sendChunk("<label>开始</label><input type='datetime-local' name='start' value='");
        Esp32BaseWeb::writeHtmlEscaped(startTime);
        Esp32BaseWeb::sendChunk("'><label>结束</label><input type='datetime-local' name='end' value='");
        Esp32BaseWeb::writeHtmlEscaped(endTime);
        Esp32BaseWeb::sendChunk("'>");
    }
    Esp32BaseWeb::sendChunk("<label>类型</label><select name='type'>");
    const char* typeValues[] = {"all", "plan", "manual", "guard"};
    const char* typeLabels[] = {"全部类型", "计划执行", "手动执行", "保护触发"};
    for (uint8_t i = 0; i < 4; ++i) {
        Esp32BaseWeb::sendChunk("<option value='");
        Esp32BaseWeb::sendChunk(typeValues[i]);
        Esp32BaseWeb::sendChunk(strcmp(type, typeValues[i]) == 0 ? "' selected>" : "'>");
        Esp32BaseWeb::sendChunk(typeLabels[i]);
        Esp32BaseWeb::sendChunk("</option>");
    }
    Esp32BaseWeb::sendChunk("</select><button type='submit'>筛选</button></form>");
    Esp32BaseWeb::sendChunk("<table class='kv'><tr><th>时间</th><th>类型</th><th>结果</th></tr><tr><td>18:30</td><td>计划执行</td><td><span class='tag ok'>完成</span></td></tr><tr><td>16:10</td><td>手动执行</td><td><span class='tag ok'>完成</span></td></tr><tr><td>14:02</td><td>保护触发</td><td><span class='tag warn'>已跳过</span></td></tr></table>");
    char pagePath[40];
    char pageQuery[120];
    snprintf(pagePath, sizeof(pagePath), "/ui-records?range=%s", range);
    if (customRange) {
        snprintf(pageQuery, sizeof(pageQuery), "type=%s&start=%s&end=%s", type, startTime, endTime);
    } else {
        snprintf(pageQuery, sizeof(pageQuery), "type=%s", type);
    }
    Esp32BaseWeb::Pagination pagination = {pagePath, pageQuery, page, perPage, 128};
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
    Esp32BaseWeb::sendInfoRowInlineEdit("row-channel-name", "第 1 路名称",
                                        "用于页面和记录展示，不影响实际控制。", "花坛",
                                        "/ui-config/name", "name", "花坛", "编辑");
    Esp32BaseWeb::sendInfoRowDialogForm("dlg-plan", "row-plan", "默认计划",
                                        "包含时间、通道、执行天数和目标量。", "3 条",
                                        "/ui-config/dialog",
                                        "<p><label>目标量</label><input type='number' name='limit' min='1' max='120' value='30'></p><p><label>模式</label><select name='mode'><option value='auto'>auto</option><option value='manual'>manual</option></select></p>",
                                        "快速编辑");
    Esp32BaseWeb::sendChunk("<div class='urow'><div><b>原生确认弹层</b><small>业务可直接使用 &lt;dialog class='panel eb-modal'&gt; 承载 1-3 个字段。</small></div><div class='uactions'><span class='uvalue'>native</span><button type='button' class='btnlink info' onclick=\"document.getElementById('native-confirm').showModal()\">打开</button></div></div>");
    Esp32BaseWeb::sendChunk("<dialog id='native-confirm' class='panel eb-modal'><h2>手动浇水确认</h2><form method='dialog'><div class='fieldgrid'><p class='field med'><label>通道</label><select><option>花坛</option><option>菜地</option></select></p><p class='field short'><label>时长</label><input type='number' min='1' max='30' value='5'><small>分钟。</small></p></div><div class='actions'><button type='submit' class='secondary' value='cancel'>取消</button><button type='submit' value='ok'>确认</button></div></form></dialog>");
    Esp32BaseWeb::sendInfoRowCompactLink("恢复出厂", "高风险操作必须进入确认保护页。", nullptr, "/ui-config/confirm", "确认页", Esp32BaseWeb::UI_DANGER);
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
    Esp32BaseWeb::sendPageTitle("分步操作", "用于校准、首次设置和复杂维护。");
    sendGalleryResults();
    Esp32BaseWeb::beginPanel("三步操作");
    Esp32BaseWeb::sendInfoRowCompact("1. 依据", "展示旧值、数据来源和是否允许继续。", "通过");
    Esp32BaseWeb::sendInfoRowCompactLink("2. 实测", "录入真实测量结果，不塞进普通配置表。", nullptr, "/ui-form", "继续");
    Esp32BaseWeb::sendInfoRowCompactLink("3. 核对保存", "展示旧值、新值、变化幅度和影响范围。", nullptr, "/ui-config/flow?saved=1", "保存");
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
    Esp32BaseWeb::sendInfoRowCompactLink("访问控制", "登录、权限不足、会话失效和只读受限状态。", nullptr, "/ui-status/access", "查看");
    Esp32BaseWeb::sendInfoRowCompactLink("空状态", "列表、记录或配置项暂不存在时的基准表达。", nullptr, "/ui-records/empty", "查看");
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
    Esp32BaseWeb::sendChunk("<form class='editform' method='post' action='/ui-form/save' onsubmit='return once(this)'><div class='fieldgrid'><p class='field med'><label>名称</label><input name='name' maxlength='16' value='花坛计划'><small>页面和记录显示名称。</small></p><p class='field short'><label>目标量</label><input type='number' name='limit' min='1' max='120' value='30'><small>1-120。</small></p><p class='field short'><label>模式</label><select name='mode'><option>auto</option><option>manual</option></select><small>执行策略。</small></p><p class='field long'><label>说明</label><input name='note' maxlength='24' value='工作日傍晚执行'><small>可选，帮助区分相似计划。</small></p></div><div class='actions'><input type='submit' value='保存'><input type='button' value='取消' onclick=\"location.href='/ui-config'\"></div></form>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void setup() {
    Esp32Base::setFirmwareInfo("web-ui-gallery", "1.0.0");
    Esp32BaseWeb::setDefaultAuth("admin", "admin");
    Esp32BaseWeb::setDeviceName("UI 样式");
    Esp32BaseWeb::setHomeMode(Esp32BaseWeb::HOME_APP);
    Esp32BaseWeb::setSystemNavMode(Esp32BaseWeb::SYSTEM_NAV_SECTION);
    Esp32BaseWeb::setHeadExtraCallback(handleHeadExtra);
    Esp32BaseAppConfig::setTitle("App Config");
    Esp32BaseAppConfig::addGroup({"display", "Display"});
    Esp32BaseAppConfig::addString({"display", GALLERY_NS, GALLERY_KEY_TITLE, "Device title", "UI Gallery", 1, 24,
                                   "System-level sample field shown in the built-in App Config page.", false, nullptr});
    Esp32BaseAppConfig::addInt({"display", GALLERY_NS, GALLERY_KEY_INTERVAL, "Refresh interval", 30, 5, 300, 5, "s",
                                "Example numeric system setting.", false, nullptr});
    Esp32BaseAppConfig::addBool({"display", GALLERY_NS, GALLERY_KEY_READONLY, "Read-only mode", false,
                                 "Example boolean switch for App Config visibility.", false, nullptr});
    Esp32BaseWeb::addPage("/index", "总览", handleStatusPage);
    Esp32BaseWeb::addRoute("/ui-status", Esp32BaseWeb::METHOD_GET, handleStatusPage);
    Esp32BaseWeb::addPage("/ui-records", "记录", handleRecordsPage);
    Esp32BaseWeb::addPage("/ui-config", "配置", handleConfigPage);
    Esp32BaseWeb::addPage("/ui-action", "命令", handleActionPage);
    Esp32BaseWeb::addPage("/ui-form", "表单", handleFormPage);
    Esp32BaseWeb::addRoute("/ui-status/stats", Esp32BaseWeb::METHOD_GET, handleStatsPage);
    Esp32BaseWeb::addRoute("/ui-config/flow", Esp32BaseWeb::METHOD_GET, handleFlowPage);
    Esp32BaseWeb::addRoute("/ui-status/maintenance", Esp32BaseWeb::METHOD_GET, handleMaintenancePage);
    Esp32BaseWeb::addRoute("/ui-status/access", Esp32BaseWeb::METHOD_GET, handleAccessPage);
    Esp32BaseWeb::addRoute("/ui-config/confirm", Esp32BaseWeb::METHOD_GET, handleConfirmPage);
    Esp32BaseWeb::addRoute("/ui-records/empty", Esp32BaseWeb::METHOD_GET, handleEmptyPage);
    Esp32BaseWeb::addRoute("/ui-action/run", Esp32BaseWeb::METHOD_POST, handleActionRun);
    Esp32BaseWeb::addRoute("/ui-confirm/run", Esp32BaseWeb::METHOD_POST, handleConfirmRun);
    Esp32BaseWeb::addRoute("/ui-form/save", Esp32BaseWeb::METHOD_POST, handleFormSave);
    Esp32BaseWeb::addRoute("/ui-config/name", Esp32BaseWeb::METHOD_POST, handleConfigNameSave);
    Esp32BaseWeb::addRoute("/ui-config/dialog", Esp32BaseWeb::METHOD_POST, handleConfigDialogSave);
    Esp32Base::begin();
    ESP32BASE_LOG_I("example", "web ui gallery ready");
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
