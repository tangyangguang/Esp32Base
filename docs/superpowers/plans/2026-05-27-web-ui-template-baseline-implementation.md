# Web UI Template Baseline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a lightweight reusable Web UI baseline to Esp32Base so built-in and business pages can share compact layout, styling, pagination, form-result notices, access-control states, statistics summaries, guided flows, and diagnostic-maintenance structures.

**Architecture:** Keep the existing synchronous `WebServer` model and current `Esp32BaseWeb::sendHeader()` / `sendFooter()` / `sendChunk()` flow. Add small C++ output helpers and CSS classes inside `Esp32BaseWeb`, then demonstrate business usage through examples before migrating built-in pages opportunistically. Style iteration is expected: visual changes must stay concentrated in `WEB_HEAD` CSS variables/classes and helper markup, not copied into business project pages.

**Tech Stack:** Arduino ESP32, synchronous `WebServer`, chunked HTML output through `Esp32BaseWeb::sendChunk()`, PROGMEM CSS, PlatformIO examples.

---

## File Structure

- Modify `src/web/Esp32BaseWeb.h`
  - Add small public UI helper enums and structs.
  - Add helper APIs for page titles, panels, notices, metrics, rows, pagination, and result notices.
- Modify `src/web/Esp32BaseWeb.inc`
  - Replace the current base CSS in `WEB_HEAD` with the compact UI baseline while preserving existing class compatibility.
  - Implement helper functions using existing chunked output and escaping functions.
- Keep App Config scoped CSS separate unless a verified implementation change proves merging saves bytes.
- Modify `examples/full_demo/src/main.cpp`
  - Add example pages for status overview, statistics summary, paginated records, compact configuration, operation command, guided flow, diagnostic maintenance, and access-control states.
  - Keep examples self-contained and deterministic; they are the visual regression target.
- Modify `examples/full_demo/platformio.ini` only if route limits or build flags require adjustment.
- Modify `docs/04_web.md`
  - Document the UI helper APIs, template responsibilities, PRG behavior, pagination rules, and skinning strategy.
- Modify `docs/11_web_ui_baseline.md`
  - Keep the canonical page capability, structure, style, skinning, and adjustment-loop documentation in sync with implementation.
- Modify `README.md`
  - Add a short Web UI baseline section that points users to `docs/04_web.md` and `full_demo`.
- Modify `CHANGELOG.md`
  - Record the new UI helper API, default CSS behavior, migration guidance, and resource boundary.
- Keep `docs/superpowers/specs/2026-05-27-web-ui-template-baseline-design.md`
  - Treat it as the design source; update only if implementation reveals a design mismatch.

## Design Constraints

- Do not introduce a front-end framework, SPA routing, WebSocket dependency, runtime theme editor, chart library, or image-based skin system.
- Do not require business projects to copy the prototype HTML.
- Do not make App Config the first migration target; it currently works and has scoped CSS.
- Keep style changes centralized in base CSS and helper-generated HTML.
- Every POST helper or example must avoid refresh re-submit by using `POST -> 303 -> GET`.
- Each template class must work on desktop and mobile without text overlap.

---

### Task 0: Canonical Web UI Baseline Documentation

**Files:**
- Create: `docs/11_web_ui_baseline.md`
- Modify: `docs/04_web.md`
- Modify: `README.md`
- Modify: `docs/superpowers/plans/2026-05-27-web-ui-template-baseline-implementation.md`

- [x] **Step 1: Create the canonical documentation page**

Create `docs/11_web_ui_baseline.md` with:

```text
定位、设计目标、页面能力分类、基础壳层、首版页面能力、样式基线、皮肤策略、与现有 Web 代码的关系、PRG 防重复提交、示例验证、样式调整回路和相关文档。
```

- [x] **Step 2: Link the canonical document from Web docs**

Add a reference near the top of `docs/04_web.md`:

```markdown
页面结构、样式基线、业务页面模板、换肤策略和视觉调整规则见 [Web UI 页面结构与样式基线](11_web_ui_baseline.md)。
```

- [x] **Step 3: Link the canonical document from README**

Add a README entry:

```markdown
Web 页面结构、样式基线、业务页面模板和换肤策略详见 [Web UI 页面结构与样式基线](docs/11_web_ui_baseline.md)。
```

- [ ] **Step 4: Keep the canonical document updated during implementation**

Whenever a later task changes helper names, CSS variable names, template scope, or style adjustment rules, update:

```text
docs/11_web_ui_baseline.md
docs/04_web.md
README.md
CHANGELOG.md
```

Expected:

```text
The dedicated document remains the long-term entry point for Web UI structure and style decisions.
```

---

### Task 1: Add UI Helper API Surface

**Files:**
- Modify: `src/web/Esp32BaseWeb.h`
- Modify: `src/web/Esp32BaseWeb.inc`
- Test: `examples/full_demo/src/main.cpp`

- [ ] **Step 1: Add public enums and structs to `Esp32BaseWeb`**

In `src/web/Esp32BaseWeb.h`, inside `class Esp32BaseWeb public:` after `enum BuiltinPage`, add:

```cpp
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
```

- [ ] **Step 2: Add helper declarations to `Esp32BaseWeb`**

In `src/web/Esp32BaseWeb.h`, after `static void sendFooter();`, add:

```cpp
    static void sendPageTitle(const char* title, const char* subtitle = nullptr);
    static void beginPanel(const char* title = nullptr);
    static void endPanel();
    static void sendNotice(UiTone tone, const char* title, const char* message = nullptr);
    static void sendResultNotice(const ResultNotice* notices, uint8_t count);
    static void beginMetricGrid();
    static void sendMetric(const char* label, const char* value, const char* help = nullptr);
    static void endMetricGrid();
    static void sendInfoRowCompact(const char* title, const char* help, const char* value = nullptr,
                                   const char* trustedActionHtml = nullptr);
    static void sendInfoRowCompactLink(const char* title, const char* help, const char* value,
                                       const char* href, const char* label, UiTone tone = UI_INFO);
    static void sendInfoRowCompactForm(const char* title, const char* help, const char* value,
                                       const char* action, const char* label,
                                       const char* hiddenName = nullptr, const char* hiddenValue = nullptr,
                                       UiTone tone = UI_INFO);
    static void sendPagination(const Pagination& pagination);
```

- [ ] **Step 3: Add private tone helper in `Esp32BaseWeb.inc`**

In `src/web/Esp32BaseWeb.inc`, inside the anonymous namespace near other small helpers, add:

```cpp
const char* uiToneClass(Esp32BaseWeb::UiTone tone) {
    switch (tone) {
        case Esp32BaseWeb::UI_OK: return " ok";
        case Esp32BaseWeb::UI_WARN: return " warn";
        case Esp32BaseWeb::UI_DANGER: return " danger";
        case Esp32BaseWeb::UI_INFO: return " info";
        case Esp32BaseWeb::UI_NEUTRAL:
        default: return "";
    }
}
```

- [ ] **Step 4: Build to confirm declarations do not break current code**

Run:

```bash
pio run -d examples/full_demo
```

Expected:

```text
SUCCESS
```

- [ ] **Step 5: Commit API surface**

```bash
git add src/web/Esp32BaseWeb.h src/web/Esp32BaseWeb.inc
git commit -m "feat(web): add UI helper API surface"
```

---

### Task 2: Replace Base CSS With Compact UI Baseline

**Files:**
- Modify: `src/web/Esp32BaseWeb.inc`
- Test: `examples/full_demo`

- [ ] **Step 1: Update `WEB_HEAD` CSS variables and base layout**

In `src/web/Esp32BaseWeb.inc`, replace the start of `WEB_HEAD` from the opening `<style>` through the base `body`, heading, link, `nav`, `.page`, `.pagehead`, `.panel`, and `.actions` rules with this compact baseline. Keep the existing `once(f)` script unchanged.

```cpp
    "<style>"
    ":root{color-scheme:light;--eb-bg:#f5f6f7;--eb-surface:#fff;--eb-soft:#f8fafb;--eb-ink:#17202a;--eb-muted:#667085;--eb-line:#d7dde4;--eb-line-soft:#edf1f4;--eb-primary:#14685d;--eb-primary-soft:#e6f2ef;--eb-ok:#087443;--eb-ok-soft:#e7f4ee;--eb-warn:#9a5a0a;--eb-warn-soft:#fff6e8;--eb-danger:#ad3027;--eb-danger-soft:#fff1ef;--eb-info:#245f9e;--eb-info-soft:#e8f0f8}"
    "*{box-sizing:border-box}body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Arial,sans-serif;font-size:14px;line-height:1.42;margin:0;padding:12px 14px 36px;color:var(--eb-ink);background:var(--eb-bg);max-width:1180px;margin-inline:auto}"
    "h1,h2,h3{color:#111827;line-height:1.25;margin:0 0 8px}h1{font-size:23px}h2{font-size:17px}h3{font-size:15px}p{margin:0 0 9px}"
    "a{color:#25313f;text-decoration:none}.muted,.info{color:var(--eb-muted)}.ok{color:var(--eb-ok)}.err,.dangertext{color:var(--eb-danger)}"
    "nav,.panel,.footerbar,.pagehead{background:var(--eb-surface);border:1px solid var(--eb-line);border-radius:8px;box-shadow:0 1px 2px rgba(16,24,40,.04)}"
    "nav{display:flex;align-items:center;gap:4px;overflow:auto;white-space:nowrap;margin:0 0 12px;padding:3px}.brand{font-weight:800}nav a{display:inline-flex;align-items:center;min-height:29px;padding:0 10px;margin:0;border-radius:6px;color:#344054;font-size:13px;font-weight:750}nav a.active{background:var(--eb-primary-soft);color:var(--eb-primary)}"
    ".page{margin:0}.pagehead{padding:12px;margin:0 0 12px}.pagehead h1{margin:0}.pagehead p{margin:4px 0 0;color:var(--eb-muted)}.panel{padding:12px;margin:12px 0}.panel>:last-child{margin-bottom:0}.actions{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-top:10px}"
```

- [ ] **Step 2: Add template classes before the existing table rules**

Still inside `WEB_HEAD`, before the existing `.kv{...}` rule, add:

```cpp
    ".notice{border:1px solid var(--eb-line);background:var(--eb-soft);border-radius:8px;padding:9px 10px;margin:8px 0;color:var(--eb-ink)}.notice.ok{border-color:#bfe0d4;background:var(--eb-ok-soft);color:#185b4e}.notice.warn{border-color:#efcf96;background:var(--eb-warn-soft);color:#7a4608}.notice.danger{border-color:#ebb8b2;background:var(--eb-danger-soft);color:#84251f}.notice.info{border-color:#c6d7ea;background:var(--eb-info-soft);color:#245f9e}"
    ".metrics{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px;margin:8px 0}.metric{border:1px solid var(--eb-line-soft);border-radius:7px;background:#fff;padding:8px}.metric b{display:block;font-size:18px}.metric span{display:block;color:var(--eb-muted);font-size:12px}"
    ".urow{display:grid;grid-template-columns:1fr auto;gap:10px;align-items:center;padding:9px 0;border-bottom:1px solid var(--eb-line-soft)}.urow:last-child{border-bottom:0}.urow small{display:block;color:var(--eb-muted);margin-top:2px}.uvalue{font-weight:800;white-space:nowrap}.pagination{display:grid;grid-template-columns:1fr auto 1fr;gap:8px;align-items:center;border-top:1px solid var(--eb-line-soft);padding-top:8px;margin-top:8px}.pagination .right{text-align:right}"
    ".tag{display:inline-flex;align-items:center;min-height:21px;padding:2px 7px;border-radius:999px;background:#eef2f4;color:#475467;font-size:11px;font-weight:750;white-space:nowrap}.tag.ok{background:var(--eb-ok-soft);color:var(--eb-ok)}.tag.warn{background:var(--eb-warn-soft);color:var(--eb-warn)}.tag.danger{background:var(--eb-danger-soft);color:var(--eb-danger)}.tag.info{background:var(--eb-info-soft);color:var(--eb-info)}"
```

- [ ] **Step 3: Preserve form compatibility while improving density**

Replace the existing button and text-input CSS strings with:

```cpp
    "button,input[type=submit],input[type=button]{font-size:14px;background:var(--eb-primary);color:#fff;min-height:32px;padding:0 11px;border:0;border-radius:7px;cursor:pointer;margin:0}button:hover,input[type=submit]:hover,input[type=button]:hover{background:#10574e}.dangerbtn,input.danger{background:#b93a32}.dangerbtn:hover,input.danger:hover{background:#9f2f29}"
    "label{display:block;font-weight:700;margin:0 0 4px}input:not([type]),input[type=text],input[type=password],input[type=number],input[type=email],input[type=url],input[type=tel],input[type=search],select{font-size:14px;min-height:32px;padding:6px 9px;margin:0 0 10px;border:1px solid #ccd5dd;border-radius:7px;box-sizing:border-box;background:#fff;color:#1f2933}"
    "input:not([type]),input[type=text],input[type=password],input[type=email],input[type=url],input[type=tel],input[type=search]{width:100%}input[type=number]{max-width:14ch}select{max-width:260px}"
```

- [ ] **Step 4: Update mobile rules**

Replace the existing `@media(max-width:640px)` rule in `WEB_HEAD` with:

```cpp
    "@media(max-width:760px){body{padding:10px}.metrics,.pagination,.urow{grid-template-columns:1fr}.pagination .right{text-align:left}.heap{margin-left:0;text-align:left;white-space:normal}nav a{font-size:13px;padding:0 9px}.tag{white-space:normal}}"
```

- [ ] **Step 5: Build and inspect CSS size risk**

Run:

```bash
pio run -d examples/full_demo
```

Expected:

```text
SUCCESS
```

Then run:

```bash
python3 - <<'PY'
from pathlib import Path
s=Path('src/web/Esp32BaseWeb.inc').read_text()
start=s.index('static const char WEB_HEAD[] PROGMEM =')
end=s.index('#if ESP32BASE_ENABLE_APP_CONFIG')
print(end-start)
PY
```

Expected:

```text
An integer under 9000
```

- [ ] **Step 6: Commit CSS baseline**

```bash
git add src/web/Esp32BaseWeb.inc
git commit -m "feat(web): add compact UI CSS baseline"
```

---

### Task 3: Implement Structure Helpers

**Files:**
- Modify: `src/web/Esp32BaseWeb.inc`
- Test: `examples/full_demo`

- [ ] **Step 1: Implement page title and panel helpers**

In `src/web/Esp32BaseWeb.inc`, after `void Esp32BaseWeb::sendFooter()`, add:

```cpp
void Esp32BaseWeb::sendPageTitle(const char* title, const char* subtitle) {
    sendChunk("<header class='pagehead'><h1>");
    writeHtmlEscaped(title ? title : "");
    sendChunk("</h1>");
    if (subtitle && subtitle[0]) {
        sendChunk("<p>");
        writeHtmlEscaped(subtitle);
        sendChunk("</p>");
    }
    sendChunk("</header>");
}

void Esp32BaseWeb::beginPanel(const char* title) {
    sendChunk("<section class='panel'>");
    if (title && title[0]) {
        sendChunk("<h2>");
        writeHtmlEscaped(title);
        sendChunk("</h2>");
    }
}

void Esp32BaseWeb::endPanel() {
    sendChunk("</section>");
}
```

- [ ] **Step 2: Implement notice and result helpers**

In the same area, add:

```cpp
void Esp32BaseWeb::sendNotice(UiTone tone, const char* title, const char* message) {
    sendChunk("<div class='notice");
    sendChunk(uiToneClass(tone));
    sendChunk("'><b>");
    writeHtmlEscaped(title ? title : "");
    sendChunk("</b>");
    if (message && message[0]) {
        sendChunk("<br>");
        writeHtmlEscaped(message);
    }
    sendChunk("</div>");
}

void Esp32BaseWeb::sendResultNotice(const ResultNotice* notices, uint8_t count) {
    if (!notices || count == 0) {
        return;
    }
    char value[32];
    for (uint8_t i = 0; i < count; ++i) {
        const ResultNotice& notice = notices[i];
        if (!notice.param || !notice.param[0]) {
            continue;
        }
        if (!hasParam(notice.param)) {
            continue;
        }
        if (notice.value && notice.value[0]) {
            if (!getParam(notice.param, value, sizeof(value)) || strcmp(value, notice.value) != 0) {
                continue;
            }
        }
        sendNotice(notice.tone, notice.title, notice.message);
        return;
    }
}
```

- [ ] **Step 3: Implement metric helpers**

Add:

```cpp
void Esp32BaseWeb::beginMetricGrid() {
    sendChunk("<div class='metrics'>");
}

void Esp32BaseWeb::sendMetric(const char* label, const char* value, const char* help) {
    sendChunk("<div class='metric'><b>");
    writeHtmlEscaped(value ? value : "");
    sendChunk("</b><span>");
    writeHtmlEscaped(label ? label : "");
    if (help && help[0]) {
        sendChunk(" · ");
        writeHtmlEscaped(help);
    }
    sendChunk("</span></div>");
}

void Esp32BaseWeb::endMetricGrid() {
    sendChunk("</div>");
}
```

- [ ] **Step 4: Implement compact row helper**

Add:

```cpp
void Esp32BaseWeb::sendInfoRowCompact(const char* title, const char* help, const char* value, const char* trustedActionHtml);
void Esp32BaseWeb::sendInfoRowCompactLink(const char* title, const char* help, const char* value, const char* href, const char* label, UiTone tone);
void Esp32BaseWeb::sendInfoRowCompactForm(const char* title, const char* help, const char* value, const char* action, const char* label, const char* hiddenName, const char* hiddenValue, UiTone tone);
```

`trustedActionHtml` is a raw static escape hatch. Prefer the link/form helpers for business pages.

- [ ] **Step 5: Build**

Run:

```bash
pio run -d examples/full_demo
```

Expected:

```text
SUCCESS
```

- [ ] **Step 6: Commit helpers**

```bash
git add src/web/Esp32BaseWeb.h src/web/Esp32BaseWeb.inc
git commit -m "feat(web): add reusable UI structure helpers"
```

---

### Task 4: Implement Pagination Helper

**Files:**
- Modify: `src/web/Esp32BaseWeb.inc`
- Test: `examples/full_demo`

- [ ] **Step 1: Add URL append helper**

Inside the anonymous namespace in `src/web/Esp32BaseWeb.inc`, add:

```cpp
void sendPaginationLink(const char* label, const Esp32BaseWeb::Pagination& pagination, uint32_t page, bool disabled) {
    if (disabled) {
        sendChunk("<span class='tag'>");
        sendEscapedHtmlChunk(label);
        sendChunk("</span>");
        return;
    }
    sendChunk("<a class='tag info' href='");
    sendEscapedHtmlChunk(pagination.path ? pagination.path : "/esp32base");
    sendChunk("?");
    if (pagination.query && pagination.query[0]) {
        sendEscapedHtmlChunk(pagination.query);
        sendChunk("&amp;");
    }
    sendChunk("page=");
    char value[16];
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(page));
    sendChunk(value);
    sendChunk("'>");
    sendEscapedHtmlChunk(label);
    sendChunk("</a>");
}
```

- [ ] **Step 2: Implement `sendPagination()`**

After `sendInfoRowCompact()`, add:

```cpp
void Esp32BaseWeb::sendPagination(const Pagination& pagination) {
    const uint32_t perPage = pagination.perPage == 0 ? 20 : pagination.perPage;
    const uint32_t totalPages = pagination.total == 0 ? 1 : ((pagination.total + perPage - 1) / perPage);
    uint32_t page = pagination.page == 0 ? 1 : pagination.page;
    if (page > totalPages) {
        page = totalPages;
    }
    char left[64];
    snprintf(left, sizeof(left), "共 %lu 条 / %lu 页", static_cast<unsigned long>(pagination.total), static_cast<unsigned long>(totalPages));
    char right[48];
    snprintf(right, sizeof(right), "当前第 %lu 页", static_cast<unsigned long>(page));
    sendChunk("<div class='pagination'><span class='muted'>");
    sendEscapedHtmlChunk(left);
    sendChunk("</span><span>");
    sendPaginationLink("首页", pagination, 1, page <= 1);
    sendChunk(" ");
    sendPaginationLink("上一页", pagination, page > 1 ? page - 1 : 1, page <= 1);
    sendChunk(" ");
    sendPaginationLink("下一页", pagination, page < totalPages ? page + 1 : totalPages, page >= totalPages);
    sendChunk(" ");
    sendPaginationLink("尾页", pagination, totalPages, page >= totalPages);
    sendChunk("</span><span class='right muted'>");
    sendEscapedHtmlChunk(right);
    sendChunk("</span></div>");
}
```

- [ ] **Step 3: Build**

Run:

```bash
pio run -d examples/full_demo
```

Expected:

```text
SUCCESS
```

- [ ] **Step 4: Commit pagination helper**

```bash
git add src/web/Esp32BaseWeb.inc
git commit -m "feat(web): add pagination helper"
```

---

### Task 5: Add Full Demo UI Baseline Pages

**Files:**
- Modify: `examples/full_demo/src/main.cpp`
- Test: `examples/full_demo`

- [ ] **Step 1: Add result notice table and status overview page**

In `examples/full_demo/src/main.cpp`, near other demo handlers, add:

```cpp
const Esp32BaseWeb::ResultNotice DEMO_RESULTS[] = {
    {"saved", "1", Esp32BaseWeb::UI_OK, "保存成功", "页面已通过 POST -> 303 -> GET 返回。"},
    {"error", "invalid", Esp32BaseWeb::UI_WARN, "提交未保存", "示例参数未通过校验。"}
};

void handleUiStatusDemo() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Status");
    Esp32BaseWeb::sendPageTitle("状态概览模板", "用于设备首页和需要快速判断当前状态的页面。");
    Esp32BaseWeb::sendResultNotice(DEMO_RESULTS, 2);
    Esp32BaseWeb::beginPanel("当前状态");
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "设备正常", "当前空闲，下一计划 18:30。");
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("当前状态", "空闲");
    Esp32BaseWeb::sendMetric("下一计划", "18:30");
    Esp32BaseWeb::sendMetric("可用通道", "4 路");
    Esp32BaseWeb::sendMetric("异常", "0");
    Esp32BaseWeb::endMetricGrid();
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}
```

- [ ] **Step 2: Add records and pagination page**

Add:

```cpp
void handleUiRecordsDemo() {
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
    Esp32BaseWeb::sendPageTitle("列表记录模板", "包含紧凑筛选、表头、分页和空状态。");
    Esp32BaseWeb::beginPanel("最近记录");
    Esp32BaseWeb::sendChunk("<div class='actions'><span class='tag info'>最近 24 小时</span><span class='tag'>全部类型</span><span class='tag'>每页 20 条</span></div>");
    Esp32BaseWeb::sendChunk("<table class='kv'><tr><th>时间</th><th>类型</th><th>结果</th></tr><tr><td>18:30</td><td>计划执行</td><td>完成</td></tr><tr><td>16:10</td><td>手动执行</td><td>完成</td></tr></table>");
    Esp32BaseWeb::Pagination p = {"/ui-records", "range=24h&type=all", page, 20, 128};
    Esp32BaseWeb::sendPagination(p);
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}
```

- [ ] **Step 3: Add configuration and operation pages**

Add:

```cpp
void handleUiConfigDemo() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Config");
    Esp32BaseWeb::sendPageTitle("配置编辑模板", "简单字段行内改，多字段对象进入独立编辑页。");
    Esp32BaseWeb::sendResultNotice(DEMO_RESULTS, 2);
    Esp32BaseWeb::beginPanel("紧凑配置列表");
    Esp32BaseWeb::sendInfoRowCompactLink("第 1 路名称", "用于页面和记录展示，不影响实际控制。", "花坛", "/ui-config?saved=1", "展开修改");
    Esp32BaseWeb::sendInfoRowCompactLink("默认浇水计划", "包含时间、通道、执行天数和目标量。", "3 条", "/ui-flow", "进入编辑页");
    Esp32BaseWeb::sendInfoRowCompactLink("恢复出厂", "高风险操作，必须进入确认保护页。", nullptr, "/ui-maintenance", "确认页", Esp32BaseWeb::UI_DANGER);
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleUiActionDemo() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Action");
    Esp32BaseWeb::sendPageTitle("操作命令模板", "一次性动作与长期配置分离。");
    Esp32BaseWeb::beginPanel("立即执行");
    Esp32BaseWeb::sendResultNotice(DEMO_RESULTS, 2);
    Esp32BaseWeb::sendInfoRowCompactForm("可执行", "设备空闲、时间可信、无严重异常。", nullptr, "/ui-action/run", "开始", "run", "1");
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "拒绝执行示例", "当前时间未同步时，应说明原因和下一步。");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}
```

- [ ] **Step 4: Add statistics, flow, maintenance, and access pages**

Add:

```cpp
void handleUiStatsDemo() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Stats");
    Esp32BaseWeb::sendPageTitle("统计摘要模板", "用轻量数字、表格和 CSS 结构表达汇总，不引入图表库。");
    Esp32BaseWeb::beginPanel("本月摘要");
    Esp32BaseWeb::beginMetricGrid();
    Esp32BaseWeb::sendMetric("本月次数", "128");
    Esp32BaseWeb::sendMetric("本月用量", "32.4L");
    Esp32BaseWeb::sendMetric("成功率", "98%");
    Esp32BaseWeb::sendMetric("较上周", "+6%");
    Esp32BaseWeb::endMetricGrid();
    Esp32BaseWeb::sendInfoRowCompact("分组汇总", "用紧凑表格或 CSS 条形表达，不依赖图表库。", "4 组", "<a class='tag info' href='/ui-records'>查看记录</a>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleUiFlowDemo() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Flow");
    Esp32BaseWeb::sendPageTitle("流程向导模板", "用于校准、首次设置和复杂维护。");
    Esp32BaseWeb::beginPanel("校准流程");
    Esp32BaseWeb::sendInfoRowCompact("1. 依据", "展示旧值、数据来源和是否允许继续。", "通过", nullptr);
    Esp32BaseWeb::sendInfoRowCompact("2. 实测", "录入真实测量结果，不塞进普通配置表。", nullptr, "<a class='tag info' href='/ui-flow'>继续</a>");
    Esp32BaseWeb::sendInfoRowCompact("3. 核对保存", "展示旧值、新值、变化幅度和影响范围。", nullptr, "<a class='tag info' href='/ui-flow?saved=1'>保存</a>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleUiMaintenanceDemo() {
    if (!Esp32BaseWeb::checkAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader("UI Maintenance");
    Esp32BaseWeb::sendPageTitle("诊断维护模板", "只读优先，危险动作进入确认保护页。");
    Esp32BaseWeb::beginPanel("系统诊断");
    Esp32BaseWeb::sendInfoRowCompact("WiFi", "连接状态、RSSI、IP。", "正常", nullptr);
    Esp32BaseWeb::sendInfoRowCompact("维护任务", "导出、扫描、重启等长任务显示状态和下一步。", "空闲", "<a class='tag info' href='/esp32base/tools'>查看</a>");
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "原始数据受控", "限制长度，可复制或导出，不做无限滚动调试平台。");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}

void handleUiAccessDemo() {
    Esp32BaseWeb::sendHeader("UI Access");
    Esp32BaseWeb::sendPageTitle("访问控制模板", "覆盖登录、权限不足、会话失效和只读受限。");
    Esp32BaseWeb::beginPanel("受限状态");
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "需要登录", "登录后可以继续执行配置和维护操作。");
    Esp32BaseWeb::sendInfoRowCompact("只读访问", "无权限时展示原因和可继续查看的内容。", "允许", "<a class='tag info' href='/esp32base'>返回状态</a>");
    Esp32BaseWeb::endPanel();
    Esp32BaseWeb::sendFooter();
}
```

- [ ] **Step 5: Register example pages**

In the setup area where `Esp32BaseWeb::addPage()` calls are made, add:

```cpp
    Esp32BaseWeb::addPage("/ui-status", "UI Status", handleUiStatusDemo);
    Esp32BaseWeb::addPage("/ui-stats", "UI Stats", handleUiStatsDemo);
    Esp32BaseWeb::addPage("/ui-records", "UI Records", handleUiRecordsDemo);
    Esp32BaseWeb::addPage("/ui-config", "UI Config", handleUiConfigDemo);
    Esp32BaseWeb::addPage("/ui-action", "UI Action", handleUiActionDemo);
    Esp32BaseWeb::addPage("/ui-flow", "UI Flow", handleUiFlowDemo);
    Esp32BaseWeb::addPage("/ui-maintenance", "UI Maintenance", handleUiMaintenanceDemo);
    Esp32BaseWeb::addPage("/ui-access", "UI Access", handleUiAccessDemo);
```

- [ ] **Step 6: Adjust route limits only if needed**

Run:

```bash
pio run -d examples/full_demo
```

If build succeeds and all pages register during existing selftest, do not change route limits.

If page registration fails due route capacity, update `examples/full_demo/platformio.ini` for `full_demo` only:

```ini
build_flags =
  -D ESP32BASE_WEB_MAX_ROUTES=24
```

Run again:

```bash
pio run -d examples/full_demo
```

Expected:

```text
SUCCESS
```

- [ ] **Step 7: Commit examples**

```bash
git add examples/full_demo/src/main.cpp examples/full_demo/platformio.ini
git commit -m "feat(examples): add Web UI baseline demo pages"
```

---

### Task 6: Visual Review and Style Iteration Gate

**Files:**
- Modify: `src/web/Esp32BaseWeb.inc`
- Modify: `examples/full_demo/src/main.cpp`
- Test: Browser or curl against a flashed device when available

- [ ] **Step 1: Record the visual review checklist**

Create a local checklist in the implementation notes section of `docs/04_web.md`:

```markdown
### Web UI baseline visual review checklist

- Desktop width: no controls stretched across empty space without reason.
- Mobile width: nav scrolls or wraps without text overlap.
- Buttons: labels remain centered and do not resize layout.
- Forms: simple single-field edit and multi-field edit are visually distinct.
- Pagination: total count, total pages, first, previous, next, last, and current page are visible.
- Result notices: success and error states are visible after redirected GET.
- Access state: login-required and read-only pages remain understandable.
- Diagnostic state: raw text is bounded, readable, and not the main page experience.
- Skinning: changing `--eb-primary` and `--eb-bg` produces a coherent page.
```

- [ ] **Step 2: Flash or run the most practical web-enabled example**

When hardware is available, run the project-specific PlatformIO environment used for Web validation. If no board is connected, run the build-only command:

```bash
pio run -d examples/full_demo
```

Expected build-only result:

```text
SUCCESS
```

- [ ] **Step 3: Review pages in browser**

Open these paths on the device or local preview target:

```text
/ui-status
/ui-stats
/ui-records
/ui-config
/ui-action
/ui-flow
/ui-maintenance
/ui-access
```

Expected:

```text
Each page renders with shared nav, compact page title, panels, readable text, and no obvious desktop/mobile overlap.
```

- [ ] **Step 4: Apply style corrections centrally**

If visual review finds spacing, color, density, or alignment issues, change only:

```text
src/web/Esp32BaseWeb.inc WEB_HEAD CSS
```

or helper markup in:

```text
src/web/Esp32BaseWeb.inc helper implementations
```

Do not patch demo pages with one-off inline CSS except for proving a hypothesis; remove any proof-only inline CSS before committing.

- [ ] **Step 5: Commit style corrections**

```bash
git add src/web/Esp32BaseWeb.inc docs/04_web.md
git commit -m "style(web): refine UI baseline after visual review"
```

---

### Task 7: Documentation and Changelog

**Files:**
- Modify: `docs/04_web.md`
- Modify: `README.md`
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Document helper API in `docs/04_web.md`**

Add this section after the existing custom page example:

```markdown
## Web UI baseline helpers

Business pages should keep using `Esp32BaseWeb::addPage()` and `Esp32BaseWeb::sendHeader()` / `sendFooter()`.

For consistent layout, use:

- `sendPageTitle(title, subtitle)` for the page title region.
- `beginPanel(title)` / `endPanel()` for page sections.
- `sendNotice(tone, title, message)` for success, warning, danger, info, and neutral states.
- `sendResultNotice(notices, count)` after redirected GET requests.
- `beginMetricGrid()` / `sendMetric()` / `endMetricGrid()` for status and statistics summaries.
- `sendInfoRowCompact(title, help, value)` for read-only configuration, operation, guided-flow, and diagnostic rows.
- `sendInfoRowCompactLink(title, help, value, href, label, tone)` for escaped link actions.
- `sendInfoRowCompactForm(title, help, value, action, label, hiddenName, hiddenValue, tone)` for single-button POST actions.
- `sendPagination(pagination)` for page-count based lists.

Forms and commands should use `POST -> 303 -> GET`. Front-end button disabling prevents double clicks only; server-side validation remains required.
```

- [ ] **Step 2: Document skinning in `docs/04_web.md`**

Add:

````markdown
### Skinning

The Web UI baseline supports CSS variable-level skinning. Business pages may override variables through `setHeadExtraCallback()`:

```cpp
void handleHeadExtra() {
    Esp32BaseWeb::sendChunk("<style>:root{--eb-primary:#245f9e;--eb-primary-soft:#e8f0f8}</style>");
}
```

Do not replace semantic color meaning. Success, warning, danger, and info colors must remain recognizable.
````

- [ ] **Step 3: Add README summary**

In `README.md`, add a short section:

```markdown
### Web UI baseline

Esp32Base includes a lightweight Web UI baseline for built-in and business pages. It provides compact layout CSS, reusable HTML helpers, result notices, pagination, access-state, statistics, guided-flow, and diagnostic-maintenance structures. See `docs/04_web.md` and `examples/full_demo` for usage.
```

- [ ] **Step 4: Add changelog entry**

In `CHANGELOG.md`, add:

```markdown
### Web UI baseline helpers

- Added lightweight Web UI helper APIs for compact page titles, panels, notices, metrics, rows, pagination, and redirected form-result notices.
- Added CSS variable-level skinning through the existing `setHeadExtraCallback()` path; no runtime theme editor, chart framework, or front-end framework is introduced.
- Added `full_demo` pages covering status overview, statistics summary, paginated records, compact configuration, operation commands, guided flow, diagnostic maintenance, and access-control states.
- Business pages can adopt the helpers incrementally while keeping existing `addPage()` and `sendHeader()` / `sendFooter()` usage.
```

- [ ] **Step 5: Build docs-adjacent example**

Run:

```bash
pio run -d examples/full_demo
```

Expected:

```text
SUCCESS
```

- [ ] **Step 6: Commit docs**

```bash
git add docs/04_web.md README.md CHANGELOG.md
git commit -m "docs(web): document UI baseline helpers"
```

---

### Task 8: Built-In Page Compatibility Pass

**Files:**
- Modify: `src/web/Esp32BaseWeb.inc`
- Test: `examples/full_demo`

- [ ] **Step 1: Verify built-in pages still use compatible classes**

Check these functions in `src/web/Esp32BaseWeb.inc`:

```text
handleRoot()
handleWifiPage()
handleOtaPage()
handleLogsPage()
handleToolsPage()
handleAuthPage()
sendAppConfigPage()
```

Expected:

```text
Existing classes `pagehead`, `panel`, `kv`, `part`, `tabs`, `quicklinks`, `logmeta`, `statusline`, `toolsection`, `logframe` are still styled by WEB_HEAD or scoped App Config CSS.
```

- [ ] **Step 2: Replace only obvious duplicated title markup**

For built-in pages that currently output:

```cpp
sendChunk("<header class='pagehead'><h1>Title</h1></header>");
```

replace with:

```cpp
Esp32BaseWeb::sendPageTitle("Title");
```

Do not rewrite App Config layout in this task.

- [ ] **Step 3: Replace built-in success/error text with notices where query params already exist**

For existing query-result messages in `handleToolsPage()` and `handleAuthPage()`, replace plain `<p class='ok'>` or `<p class='err'>` with `sendNotice(...)` only when the message already maps to a single query arg.

Example replacement:

```cpp
if (g_server.hasArg("formatted")) {
    Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "LittleFS formatted", "FileLog mode was reloaded.");
}
```

- [ ] **Step 4: Build**

Run:

```bash
pio run -d examples/full_demo
```

Expected:

```text
SUCCESS
```

- [ ] **Step 5: Commit compatibility pass**

```bash
git add src/web/Esp32BaseWeb.inc
git commit -m "refactor(web): use UI helpers in built-in pages"
```

---

### Task 9: Business Project Trial Without Wide Migration

**Files:**
- No Esp32Base source changes required unless trial reveals a base-library defect.
- Read-only or local branch changes in the irrigation project chosen for validation.

- [ ] **Step 1: Pick one irrigation page**

Choose the current highest-pain page from the irrigation project:

```text
One configuration page or one records page.
```

Do not migrate all irrigation pages in this task.

- [ ] **Step 2: Replace local page markup with helper calls**

Use the same pattern as `examples/full_demo`:

```cpp
Esp32BaseWeb::sendHeader("Page title");
Esp32BaseWeb::sendPageTitle("Page title", "Short page purpose.");
Esp32BaseWeb::beginPanel("Section");
Esp32BaseWeb::sendInfoRowCompact("Field", "One sentence explanation.", "Current value", "<a class='tag info' href='/edit'>编辑</a>");
Esp32BaseWeb::endPanel();
Esp32BaseWeb::sendFooter();
```

- [ ] **Step 3: Check whether the base API is sufficient**

If the irrigation page needs one-off CSS to look acceptable, stop and decide whether the missing concept belongs in Esp32Base helper/CSS.

Accepted outcomes:

```text
No base change needed
```

or:

```text
Add a focused helper/CSS change to Esp32Base, then update full_demo first
```

- [ ] **Step 4: Verify PRG**

For any form or command on the trial page:

```text
Submit -> server returns 303 -> browser lands on GET page -> refresh does not resubmit
```

- [ ] **Step 5: Record trial result**

Add a short note to `CHANGELOG.md` if the trial changes Esp32Base API or behavior:

```markdown
- Verified the Web UI baseline against an irrigation business page before broad migration.
```

Commit only if Esp32Base files changed:

```bash
git add src/web/Esp32BaseWeb.h src/web/Esp32BaseWeb.inc examples/full_demo/src/main.cpp docs/04_web.md README.md CHANGELOG.md
git commit -m "fix(web): refine UI baseline from irrigation trial"
```

---

## Verification Matrix

- [ ] `pio run -d examples/full_demo`
  - Expected: `SUCCESS`
- [ ] `pio run -d examples/basic -e web`
  - Expected: `SUCCESS` if the environment exists; if not, list available environments and choose the closest Web-enabled one.
- [ ] Browser visual review for `full_demo` pages:
  - `/ui-status`
  - `/ui-stats`
  - `/ui-records`
  - `/ui-config`
  - `/ui-action`
  - `/ui-flow`
  - `/ui-maintenance`
  - `/ui-access`
- [ ] Built-in page smoke review:
  - `/esp32base`
  - `/esp32base/wifi`
  - `/esp32base/logs`
  - `/esp32base/tools`
  - `/esp32base/auth`
  - `/esp32base/ota` when OTA profile is enabled
  - `/esp32base/app-config` when App Config profile is enabled
- [ ] Mobile viewport review:
  - Navigation does not overlap.
  - Rows stack cleanly.
  - Pagination remains usable.
  - Buttons remain centered.
- [ ] Skinning check:
  - Override `--eb-primary` and `--eb-primary-soft` through `setHeadExtraCallback()`.
  - Verify nav, buttons, tags, and notices remain coherent.

## Self-Review

- Spec coverage:
  - Foundation shell: Tasks 2, 3, 8.
  - Access control: Task 5 example and Task 7 docs.
  - Status overview: Task 5 example and metric helpers.
  - Statistics summary: Task 5 example and metric helpers.
  - List records and pagination: Task 4 and Task 5 records page.
  - Configuration editing: Task 5 config page and compact row helper.
  - Operation commands: Task 5 action page and PRG verification.
  - Confirmation protection: Task 8 preserves built-in dangerous POST patterns; Task 9 verifies business PRG.
  - Horizontal state: Task 3 notice helpers and Task 5 result notices.
  - Flow wizard: Task 5 flow page.
  - Diagnostic maintenance: Task 5 maintenance page.
  - Skinning: Task 2 CSS variables and Task 7 docs.
  - Style iteration readiness: Task 6.
- Placeholder scan:
  - The plan contains no unresolved marker words or unspecified implementation steps.
- Type consistency:
  - Public API names declared in Task 1 match implementations and examples in Tasks 3, 4, and 5.
