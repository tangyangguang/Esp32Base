#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB && ESP32BASE_ENABLE_APP_CONFIG

#include "WebInternal.h"

namespace esp32base_web {

#if ESP32BASE_ENABLE_APP_CONFIG
bool validConfigName(const char* value) {
    if (!value) {
        return false;
    }
    const size_t len = strlen(value);
    return len >= 1 && len <= 15;
}

bool appNsAllowed(const char* ns) {
    return validConfigName(ns) && strncmp(ns, "eb_", 3) != 0;
}

Esp32BaseAppConfig::FieldType publicFieldType(AppConfigStoredType type) {
    switch (type) {
        case APP_CFG_STRING: return Esp32BaseAppConfig::FIELD_STRING;
        case APP_CFG_INT: return Esp32BaseAppConfig::FIELD_INT;
        case APP_CFG_DECIMAL: return Esp32BaseAppConfig::FIELD_DECIMAL;
        case APP_CFG_BOOL: return Esp32BaseAppConfig::FIELD_BOOL;
        case APP_CFG_ENUM: return Esp32BaseAppConfig::FIELD_ENUM;
    }
    return Esp32BaseAppConfig::FIELD_STRING;
}

Esp32BaseAppConfig::FieldRef makeFieldRef(const AppConfigFieldSlot& field) {
    Esp32BaseAppConfig::FieldRef ref = {field.groupId, field.ns, field.key, field.label, publicFieldType(field.type), field.restartRequired};
    return ref;
}

bool groupExists(const char* groupId) {
    if (!groupId || !groupId[0]) {
        return false;
    }
    for (uint8_t i = 0; i < g_appConfigGroupCount; ++i) {
        if (strcmp(g_appConfigGroups[i].id, groupId) == 0) {
            return true;
        }
    }
    return false;
}

bool fieldExists(const char* ns, const char* key) {
    for (uint8_t i = 0; i < g_appConfigFieldCount; ++i) {
        if (strcmp(g_appConfigFields[i].ns, ns) == 0 && strcmp(g_appConfigFields[i].key, key) == 0) {
            return true;
        }
    }
    return false;
}

bool validFieldCommon(const char* groupId, const char* ns, const char* key, const char* label) {
    if (!groupExists(groupId) || !appNsAllowed(ns) || !validConfigName(key) || !label || !label[0] ||
        strlen(label) > Esp32BaseAppConfig::LABEL_MAX_LENGTH) {
        return false;
    }
    return !fieldExists(ns, key);
}

bool validOptionalHelp(const char* help) {
    return !help || strlen(help) <= Esp32BaseAppConfig::HELP_MAX_LENGTH;
}

bool validOptionalUnit(const char* unit) {
    return !unit || strlen(unit) <= Esp32BaseAppConfig::UNIT_MAX_LENGTH;
}

void appConfigLogRegisterFailed(const char* type, const char* ns, const char* key) {
    ESP32BASE_LOG_W("appcfg", "register_failed type=%s ns=%s key=%s", type ? type : "-", ns ? ns : "-", key ? key : "-");
}

bool enumValueAllowed(const Esp32BaseAppConfig::EnumField& field, const char* value) {
    if (!value) {
        return false;
    }
    for (uint8_t i = 0; i < field.optionCount; ++i) {
        if (field.options[i].value && strcmp(field.options[i].value, value) == 0) {
            return true;
        }
    }
    return false;
}

bool validEnumOptions(const Esp32BaseAppConfig::EnumField& field) {
    if (!field.options || field.optionCount == 0) {
        return false;
    }
    for (uint8_t i = 0; i < field.optionCount; ++i) {
        const char* value = field.options[i].value;
        const char* label = field.options[i].label;
        if (!value || !value[0] || strlen(value) > Esp32BaseAppConfig::ENUM_VALUE_MAX_LENGTH ||
            !label || !label[0] || strlen(label) > Esp32BaseAppConfig::LABEL_MAX_LENGTH) {
            return false;
        }
        for (uint8_t j = static_cast<uint8_t>(i + 1); j < field.optionCount; ++j) {
            if (field.options[j].value && strcmp(value, field.options[j].value) == 0) {
                return false;
            }
        }
    }
    return enumValueAllowed(field, field.defaultValue);
}

bool stepMatches(int32_t value, int32_t minValue, int32_t step) {
    if (step <= 0) {
        return false;
    }
    const int64_t diff = static_cast<int64_t>(value) - static_cast<int64_t>(minValue);
    return diff >= 0 && (diff % step) == 0;
}

bool parseStrictInt32(const char* text, int32_t& out) {
    if (!text || !text[0]) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const long value = strtol(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value < INT32_MIN || value > INT32_MAX) {
        return false;
    }
    out = static_cast<int32_t>(value);
    return true;
}

int32_t pow10Int(uint8_t scale) {
    int32_t factor = 1;
    for (uint8_t i = 0; i < scale; ++i) {
        factor *= 10;
    }
    return factor;
}

bool formatDecimalRaw(int32_t raw, uint8_t scale, char* out, size_t len) {
    if (!out || len == 0 || scale > Esp32BaseAppConfig::DECIMAL_SCALE_MAX) {
        return false;
    }
    if (scale == 0) {
        snprintf(out, len, "%ld", static_cast<long>(raw));
        return true;
    }
    const int32_t factor = pow10Int(scale);
    int64_t absValue = raw;
    const bool neg = absValue < 0;
    if (neg) {
        absValue = -absValue;
    }
    char fracFmt[8];
    snprintf(fracFmt, sizeof(fracFmt), "%%0%ulld", static_cast<unsigned>(scale));
    char fracBuf[12];
    snprintf(fracBuf, sizeof(fracBuf), fracFmt, static_cast<long long>(absValue % factor));
    snprintf(out, len, "%s%lld.%s", neg ? "-" : "", static_cast<long long>(absValue / factor), fracBuf);
    return true;
}

bool parseDecimalRaw(const char* text, uint8_t scale, int32_t& rawOut) {
    if (!text || !text[0] || scale > Esp32BaseAppConfig::DECIMAL_SCALE_MAX) {
        return false;
    }
    const char* p = text;
    bool neg = false;
    if (*p == '-' || *p == '+') {
        neg = *p == '-';
        ++p;
    }
    const int32_t factor = pow10Int(scale);
    const int64_t maxAbsRaw = neg ? (static_cast<int64_t>(INT32_MAX) + 1LL) : static_cast<int64_t>(INT32_MAX);
    int64_t whole = 0;
    bool hadDigit = false;
    while (*p >= '0' && *p <= '9') {
        hadDigit = true;
        const int digit = *p - '0';
        whole = whole * 10 + digit;
        if (whole > maxAbsRaw / factor) {
            return false;
        }
        ++p;
    }
    int64_t frac = 0;
    uint8_t fracDigits = 0;
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            if (fracDigits >= scale) {
                return false;
            }
            frac = frac * 10 + (*p - '0');
            ++fracDigits;
            ++p;
        }
    }
    if (!hadDigit || *p != '\0') {
        return false;
    }
    while (fracDigits < scale) {
        frac *= 10;
        ++fracDigits;
    }
    int64_t raw = whole * factor + frac;
    if (raw > maxAbsRaw) {
        return false;
    }
    if (neg) {
        raw = -raw;
    }
    if (raw < INT32_MIN || raw > INT32_MAX) {
        return false;
    }
    rawOut = static_cast<int32_t>(raw);
    return true;
}

void appConfigFieldName(uint8_t index, char* out, size_t len) {
    snprintf(out, len, "f%u", static_cast<unsigned>(index));
}

bool getSubmittedRaw(const AppConfigFieldSlot& field, uint8_t index, char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    char name[8];
    appConfigFieldName(index, name, sizeof(name));
    if (field.type == APP_CFG_BOOL) {
        return strlcpy(out, g_server.hasArg(name) ? "1" : "0", len) < len;
    }
    if (!g_server.hasArg(name)) {
        out[0] = '\0';
        return false;
    }
    return strlcpy(out, g_server.arg(name).c_str(), len) < len;
}

bool findSubmittedRaw(const char* ns, const char* key, char* out, size_t len, const AppConfigFieldSlot** fieldOut = nullptr) {
    for (uint8_t i = 0; i < g_appConfigFieldCount; ++i) {
        const AppConfigFieldSlot& field = g_appConfigFields[i];
        if (strcmp(field.ns, ns) == 0 && strcmp(field.key, key) == 0) {
            if (fieldOut) {
                *fieldOut = &field;
            }
            return getSubmittedRaw(field, i, out, len);
        }
    }
    return false;
}

void readAppConfigValue(const AppConfigFieldSlot& field, char* textOut, size_t textLen, int32_t& rawOut, bool& boolOut) {
    textOut[0] = '\0';
    rawOut = 0;
    boolOut = false;
    switch (field.type) {
        case APP_CFG_STRING:
            Esp32BaseConfig::getStr(field.ns, field.key, textOut, textLen, field.spec.stringField.defaultValue ? field.spec.stringField.defaultValue : "");
            break;
        case APP_CFG_INT:
            rawOut = Esp32BaseConfig::getInt(field.ns, field.key, field.spec.intField.defaultValue);
            snprintf(textOut, textLen, "%ld", static_cast<long>(rawOut));
            break;
        case APP_CFG_DECIMAL:
            rawOut = Esp32BaseConfig::getInt(field.ns, field.key, field.spec.decimalField.defaultRawValue);
            formatDecimalRaw(rawOut, field.spec.decimalField.scale, textOut, textLen);
            break;
        case APP_CFG_BOOL:
            boolOut = Esp32BaseConfig::getBool(field.ns, field.key, field.spec.boolField.defaultValue);
            strlcpy(textOut, boolOut ? "true" : "false", textLen);
            break;
        case APP_CFG_ENUM:
            Esp32BaseConfig::getStr(field.ns, field.key, textOut, textLen, field.spec.enumField.defaultValue ? field.spec.enumField.defaultValue : "");
            break;
    }
}

const char* appConfigEnumLabel(const AppConfigFieldSlot& field, const char* value) {
    if (field.type == APP_CFG_ENUM && value) {
        for (uint8_t i = 0; i < field.spec.enumField.optionCount; ++i) {
            const Esp32BaseAppConfig::EnumOption& opt = field.spec.enumField.options[i];
            if (opt.value && strcmp(opt.value, value) == 0) {
                return opt.label ? opt.label : opt.value;
            }
        }
    }
    return value ? value : "";
}

void appConfigDisplayText(const AppConfigFieldSlot& field, const char* text, bool boolean, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    switch (field.type) {
        case APP_CFG_BOOL:
            strlcpy(out, boolean ? "Enabled" : "Disabled", len);
            break;
        case APP_CFG_ENUM:
            strlcpy(out, appConfigEnumLabel(field, text), len);
            break;
        default:
            strlcpy(out, text ? text : "", len);
            break;
    }
}

void clearAppConfigPendingRestart(AppConfigPendingRestartSlot& slot) {
    if (slot.oldText) {
        free(slot.oldText);
        slot.oldText = nullptr;
    }
    slot.active = false;
    slot.fieldIndex = 0;
    slot.oldRaw = 0;
    slot.oldBool = false;
    slot.oldTextUnavailable = false;
}

AppConfigPendingRestartSlot* findAppConfigPendingRestart(uint8_t fieldIndex) {
    for (uint8_t i = 0; i < ESP32BASE_APP_CONFIG_MAX_FIELDS; ++i) {
        if (g_appConfigPendingRestart[i].active && g_appConfigPendingRestart[i].fieldIndex == fieldIndex) {
            return &g_appConfigPendingRestart[i];
        }
    }
    return nullptr;
}

AppConfigPendingRestartSlot* allocAppConfigPendingRestart(uint8_t fieldIndex) {
    AppConfigPendingRestartSlot* existing = findAppConfigPendingRestart(fieldIndex);
    if (existing) {
        return existing;
    }
    for (uint8_t i = 0; i < ESP32BASE_APP_CONFIG_MAX_FIELDS; ++i) {
        if (!g_appConfigPendingRestart[i].active) {
            g_appConfigPendingRestart[i].active = true;
            g_appConfigPendingRestart[i].fieldIndex = fieldIndex;
            return &g_appConfigPendingRestart[i];
        }
    }
    return nullptr;
}

bool appConfigValueMatchesPendingOriginal(const AppConfigFieldSlot& field, const AppConfigPendingRestartSlot& slot,
                                          const char* newText, int32_t newRaw, bool newBool) {
    switch (field.type) {
        case APP_CFG_STRING:
        case APP_CFG_ENUM:
            if (slot.oldTextUnavailable) {
                return false;
            }
            return strcmp(slot.oldText ? slot.oldText : "", newText ? newText : "") == 0;
        case APP_CFG_INT:
        case APP_CFG_DECIMAL:
            return slot.oldRaw == newRaw;
        case APP_CFG_BOOL:
            return slot.oldBool == newBool;
    }
    return false;
}

void updateAppConfigPendingRestart(const AppConfigFieldSlot& field, uint8_t index,
                                   const char* oldText, int32_t oldRaw, bool oldBool,
                                   const char* newText, int32_t newRaw, bool newBool) {
    if (!field.restartRequired) {
        return;
    }
    AppConfigPendingRestartSlot* slot = findAppConfigPendingRestart(index);
    if (!slot) {
        slot = allocAppConfigPendingRestart(index);
        if (!slot) {
            ESP32BASE_LOG_W("appcfg", "pending_restart_full key=%s", field.key ? field.key : "-");
            return;
        }
        slot->oldRaw = oldRaw;
        slot->oldBool = oldBool;
        if (field.type == APP_CFG_STRING || field.type == APP_CFG_ENUM) {
            const char* value = oldText ? oldText : "";
            slot->oldText = static_cast<char*>(malloc(strlen(value) + 1));
            if (!slot->oldText) {
                slot->oldTextUnavailable = true;
                ESP32BASE_LOG_W("appcfg", "pending_restart_alloc_failed key=%s", field.key ? field.key : "-");
                return;
            }
            strcpy(slot->oldText, value);
        }
    }
    if (appConfigValueMatchesPendingOriginal(field, *slot, newText, newRaw, newBool)) {
        clearAppConfigPendingRestart(*slot);
    }
}

bool hasAppConfigPendingRestart() {
    for (uint8_t i = 0; i < ESP32BASE_APP_CONFIG_MAX_FIELDS; ++i) {
        if (g_appConfigPendingRestart[i].active) {
            return true;
        }
    }
    return false;
}

Esp32BaseAppConfig::FieldValue makeFieldValue(const AppConfigFieldSlot& field, const char* text, int32_t raw, bool boolean) {
    Esp32BaseAppConfig::FieldValue value = {publicFieldType(field.type), text, raw, boolean};
    return value;
}

bool validateSubmittedField(const AppConfigFieldSlot& field, const char* submitted, char* normalized, size_t normalizedLen,
                            int32_t& rawOut, bool& boolOut, char* error, size_t errorLen) {
    normalized[0] = '\0';
    rawOut = 0;
    boolOut = false;
    switch (field.type) {
        case APP_CFG_STRING: {
            const size_t len = submitted ? strlen(submitted) : 0;
            if (!submitted || len < field.spec.stringField.minLength || len > field.spec.stringField.maxLength) {
                snprintf(error, errorLen, "%s length must be %u..%u.", field.label, field.spec.stringField.minLength, field.spec.stringField.maxLength);
                return false;
            }
            strlcpy(normalized, submitted, normalizedLen);
            break;
        }
        case APP_CFG_INT:
            if (!parseStrictInt32(submitted, rawOut) || rawOut < field.spec.intField.minValue ||
                rawOut > field.spec.intField.maxValue || !stepMatches(rawOut, field.spec.intField.minValue, field.spec.intField.step)) {
                snprintf(error, errorLen, "%s is out of range.", field.label);
                return false;
            }
            snprintf(normalized, normalizedLen, "%ld", static_cast<long>(rawOut));
            break;
        case APP_CFG_DECIMAL:
            if (!parseDecimalRaw(submitted, field.spec.decimalField.scale, rawOut) ||
                rawOut < field.spec.decimalField.minRawValue || rawOut > field.spec.decimalField.maxRawValue ||
                !stepMatches(rawOut, field.spec.decimalField.minRawValue, field.spec.decimalField.stepRaw)) {
                snprintf(error, errorLen, "%s is out of range.", field.label);
                return false;
            }
            formatDecimalRaw(rawOut, field.spec.decimalField.scale, normalized, normalizedLen);
            break;
        case APP_CFG_BOOL:
            boolOut = submitted && strcmp(submitted, "1") == 0;
            strlcpy(normalized, boolOut ? "true" : "false", normalizedLen);
            break;
        case APP_CFG_ENUM:
            if (!submitted || !enumValueAllowed(field.spec.enumField, submitted)) {
                snprintf(error, errorLen, "%s has an invalid option.", field.label);
                return false;
            }
            strlcpy(normalized, submitted, normalizedLen);
            break;
    }
    if (field.validate) {
        Esp32BaseAppConfig::FieldRef ref = makeFieldRef(field);
        Esp32BaseAppConfig::FieldValue value = makeFieldValue(field, normalized, rawOut, boolOut);
        if (!field.validate(ref, value, error, errorLen)) {
            if (!error[0]) {
                snprintf(error, errorLen, "%s is invalid.", field.label);
            }
            return false;
        }
    }
    return true;
}

void sendAppConfigScript() {
    sendChunk("<script>"
              "function acVal(e){return e.type=='checkbox'?(e.checked?'true':'false'):e.value;}"
              "function acText(e){if(e.tagName=='SELECT')return e.options[e.selectedIndex]?e.options[e.selectedIndex].text:e.value;if(e.type=='checkbox')return e.checked?'Enabled':'Disabled';return e.value;}"
              "function acEsc(s){return String(s).replace(/[&<>\"']/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c];});}"
              "function acBuild(f){var rows='',n=0,g='',snap=[];f.querySelectorAll('[data-ac]').forEach(function(e){var a=e.dataset.initial||'',b=acVal(e),eg=e.dataset.group||'';snap.push([e.name,b]);if(a!=b){if(eg!=g){g=eg;rows+='<tr class=\"acgroup\"><th colspan=\"3\">'+acEsc(g)+'</th></tr>';}n++;rows+='<tr><td>'+acEsc(e.dataset.label)+'</td><td>'+acEsc(e.dataset.initialText||a)+'</td><td>'+acEsc(acText(e))+'</td></tr>';}});return {rows:rows,n:n,snap:JSON.stringify(snap)};}"
              "function acShow(b,s){var y=window.scrollY||window.pageYOffset||0,raf=window.requestAnimationFrame||function(cb){setTimeout(cb,0);};b.classList.add('open');b.setAttribute('aria-hidden','false');if(s)s.classList.add('reviewing');raf(function(){window.scrollTo(0,y);});}"
              "function acHide(b,s){b.classList.remove('open');b.setAttribute('aria-hidden','true');if(s)s.classList.remove('reviewing');}"
              "function acConfirm(f){var r=acBuild(f);if(!r.n){alert('No App Config changes.');return false;}var b=document.getElementById('acbox'),t=document.getElementById('acrows'),s=document.getElementById('acsavebar');if(!f.dataset.confirmed){t.innerHTML=r.rows;f.dataset.snapshot=r.snap;acShow(b,s);return false;}if(f.dataset.snapshot!=r.snap){delete f.dataset.confirmed;t.innerHTML=r.rows;f.dataset.snapshot=r.snap;acShow(b,s);alert('App Config changed. Review changes again.');return false;}return once(f);}"
              "function acCancel(f){f=f||document.querySelector('form');var b=document.getElementById('acbox'),s=document.getElementById('acsavebar');if(f){delete f.dataset.confirmed;delete f.dataset.snapshot;}acHide(b,s);}"
              "function acDirty(f){if(f.dataset.snapshot||f.dataset.confirmed)acCancel(f);}"
              "function acSubmit(f){f.dataset.confirmed='1';if(f.requestSubmit){f.requestSubmit();}else{var b=f.querySelector('[type=submit]');if(b)b.click();}}"
              "</script>");
}

void sendAppConfigTopMessage() {
    if (g_server.hasArg("saved")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "App Config saved");
    } else if (g_server.hasArg("partial")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "Some App Config values were not saved");
    } else if (g_server.hasArg("restart")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "App Config saved", "Restart the device for marked values to take effect.");
    } else if (g_server.hasArg("stale")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "Config page was stale", "Reload and try again.");
    }
}

void sendAppConfigPendingRestartNotice() {
    if (!hasAppConfigPendingRestart()) {
        return;
    }
    sendChunk("<section class='panel pendingbox'><h2>Restart required</h2><p class='muted'>Some saved App Config values take effect after restart.</p><div class='tablewrap'><table><thead><tr><th>Parameter</th><th>Running</th><th>Saved</th></tr></thead><tbody>");
    for (uint8_t i = 0; i < ESP32BASE_APP_CONFIG_MAX_FIELDS; ++i) {
        const AppConfigPendingRestartSlot& slot = g_appConfigPendingRestart[i];
        if (!slot.active || slot.fieldIndex >= g_appConfigFieldCount) {
            continue;
        }
        const AppConfigFieldSlot& field = g_appConfigFields[slot.fieldIndex];
        char oldText[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
        char newText[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
        char display[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
        int32_t raw = 0;
        bool boolean = false;
        switch (field.type) {
            case APP_CFG_INT:
                snprintf(oldText, sizeof(oldText), "%ld", static_cast<long>(slot.oldRaw));
                break;
            case APP_CFG_DECIMAL:
                formatDecimalRaw(slot.oldRaw, field.spec.decimalField.scale, oldText, sizeof(oldText));
                break;
            case APP_CFG_BOOL:
                strlcpy(oldText, slot.oldBool ? "Enabled" : "Disabled", sizeof(oldText));
                break;
            case APP_CFG_STRING:
            case APP_CFG_ENUM:
                if (slot.oldTextUnavailable) {
                    strlcpy(oldText, "unavailable", sizeof(oldText));
                } else {
                    appConfigDisplayText(field, slot.oldText ? slot.oldText : "", false, oldText, sizeof(oldText));
                }
                break;
        }
        readAppConfigValue(field, newText, sizeof(newText), raw, boolean);
        appConfigDisplayText(field, newText, boolean, display, sizeof(display));
        sendChunk("<tr><td>");
        sendEscapedHtmlChunk(field.label);
        sendChunk("</td><td>");
        sendEscapedHtmlChunk(oldText);
        sendChunk("</td><td>");
        sendEscapedHtmlChunk(display);
        sendChunk("</td></tr>");
    }
    sendChunk("</tbody></table></div></section>");
}

void sendAppConfigPage(const char* errorMessage = nullptr) {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    g_pageHeadProgmem = WEB_APPCFG_STYLE;
    Esp32BaseWeb::sendHeader(g_appConfigTitle);
    sendAppConfigScript();
    sendChunk("<div class='appcfg'>");
    Esp32BaseWeb::sendPageTitle(g_appConfigTitle, "Application configuration values stored by Esp32Base.");
    sendAppConfigTopMessage();
    sendAppConfigPendingRestartNotice();
    if (errorMessage && errorMessage[0]) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "App Config was not saved", errorMessage);
    }
    if (g_appConfigGroupCount == 0 || g_appConfigFieldCount == 0) {
        sendChunk("<section class='panel actionpanel'><h2>No App Config fields</h2><p class='muted'>No application configuration values are registered by the app.</p></section>");
        sendChunk("</div>");
        Esp32BaseWeb::sendFooter();
        return;
    }
    char buf[32];
    sendChunk("<form method='post' action='/esp32base/app-config' onsubmit='return acConfirm(this)' oninput='acDirty(this)' onchange='acDirty(this)'><input type='hidden' name='rev' value='");
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(g_appConfigRevision));
    sendEscapedHtmlChunk(buf);
    sendChunk("'>");
    for (uint8_t g = 0; g < g_appConfigGroupCount; ++g) {
        sendChunk("<section class='panel'><h2>");
        sendEscapedHtmlChunk(g_appConfigGroups[g].title);
        sendChunk("</h2>");
        for (uint8_t i = 0; i < g_appConfigFieldCount; ++i) {
            const AppConfigFieldSlot& field = g_appConfigFields[i];
            if (strcmp(field.groupId, g_appConfigGroups[g].id) != 0) {
                continue;
            }
            char value[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
            int32_t raw = 0;
            bool boolean = false;
            readAppConfigValue(field, value, sizeof(value), raw, boolean);
            char name[8];
            appConfigFieldName(i, name, sizeof(name));
            sendChunk("<div class='acfield'><label for='");
            sendEscapedHtmlChunk(name);
            sendChunk("'>");
            sendEscapedHtmlChunk(field.label);
            if (field.restartRequired) {
                sendChunk(" <span class='tag warn restart'>restart</span>");
            }
            sendChunk("</label>");
            if (field.type == APP_CFG_BOOL) {
                sendChunk("<label><input id='");
                sendEscapedHtmlChunk(name);
                sendChunk("' type='checkbox' name='");
                sendEscapedHtmlChunk(name);
                sendChunk("' value='1' data-ac='1' data-label='");
                sendEscapedHtmlChunk(field.label);
                sendChunk("' data-group='");
                sendEscapedHtmlChunk(g_appConfigGroups[g].title);
                sendChunk("' data-initial='");
                sendEscapedHtmlChunk(boolean ? "true" : "false");
                sendChunk("' data-initial-text='");
                sendEscapedHtmlChunk(boolean ? "Enabled" : "Disabled");
                sendChunk("'");
                if (boolean) {
                    sendChunk(" checked");
                }
                sendChunk("> Enabled</label>");
            } else if (field.type == APP_CFG_ENUM) {
                sendChunk("<select id='");
                sendEscapedHtmlChunk(name);
                sendChunk("' name='");
                sendEscapedHtmlChunk(name);
                sendChunk("' data-ac='1' data-label='");
                sendEscapedHtmlChunk(field.label);
                sendChunk("' data-group='");
                sendEscapedHtmlChunk(g_appConfigGroups[g].title);
                sendChunk("' data-initial='");
                sendEscapedHtmlChunk(value);
                sendChunk("' data-initial-text='");
                for (uint8_t o = 0; o < field.spec.enumField.optionCount; ++o) {
                    const Esp32BaseAppConfig::EnumOption& opt = field.spec.enumField.options[o];
                    if (strcmp(value, opt.value) == 0) {
                        sendEscapedHtmlChunk(opt.label);
                        break;
                    }
                }
                sendChunk("'>");
                for (uint8_t o = 0; o < field.spec.enumField.optionCount; ++o) {
                    const Esp32BaseAppConfig::EnumOption& opt = field.spec.enumField.options[o];
                    sendChunk("<option value='");
                    sendEscapedHtmlChunk(opt.value);
                    sendChunk("'");
                    if (strcmp(value, opt.value) == 0) {
                        sendChunk(" selected");
                    }
                    sendChunk(">");
                    sendEscapedHtmlChunk(opt.label);
                    sendChunk("</option>");
                }
                sendChunk("</select>");
            } else {
                sendChunk("<div class='acrow'><input id='");
                sendEscapedHtmlChunk(name);
                sendChunk("' name='");
                sendEscapedHtmlChunk(name);
                sendChunk("' data-ac='1' data-label='");
                sendEscapedHtmlChunk(field.label);
                sendChunk("' data-group='");
                sendEscapedHtmlChunk(g_appConfigGroups[g].title);
                sendChunk("' data-initial='");
                sendEscapedHtmlChunk(value);
                sendChunk("' value='");
                sendEscapedHtmlChunk(value);
                sendChunk("'");
                if (field.type == APP_CFG_STRING) {
                    snprintf(buf, sizeof(buf), "%u", field.spec.stringField.maxLength);
                    sendChunk(" maxlength='");
                    sendEscapedHtmlChunk(buf);
                    sendChunk("'");
                } else {
                    sendChunk(" type='number'");
                    if (field.type == APP_CFG_INT) {
                        sendChunk(" min='");
                        snprintf(buf, sizeof(buf), "%ld", static_cast<long>(field.spec.intField.minValue));
                        sendEscapedHtmlChunk(buf);
                        sendChunk("' max='");
                        snprintf(buf, sizeof(buf), "%ld", static_cast<long>(field.spec.intField.maxValue));
                        sendEscapedHtmlChunk(buf);
                        sendChunk("' step='");
                        snprintf(buf, sizeof(buf), "%ld", static_cast<long>(field.spec.intField.step));
                        sendEscapedHtmlChunk(buf);
                        sendChunk("'");
                    } else if (field.type == APP_CFG_DECIMAL) {
                        char num[32];
                        sendChunk(" min='");
                        formatDecimalRaw(field.spec.decimalField.minRawValue, field.spec.decimalField.scale, num, sizeof(num));
                        sendEscapedHtmlChunk(num);
                        sendChunk("' max='");
                        formatDecimalRaw(field.spec.decimalField.maxRawValue, field.spec.decimalField.scale, num, sizeof(num));
                        sendEscapedHtmlChunk(num);
                        sendChunk("' step='");
                        formatDecimalRaw(field.spec.decimalField.stepRaw, field.spec.decimalField.scale, num, sizeof(num));
                        sendEscapedHtmlChunk(num);
                        sendChunk("'");
                    }
                }
                sendChunk(">");
                if (field.unit && field.unit[0]) {
                    sendChunk("<span class='unit'>");
                    sendEscapedHtmlChunk(field.unit);
                    sendChunk("</span>");
                }
                sendChunk("</div>");
            }
            if (field.help && field.help[0]) {
                sendChunk("<p class='help'>");
                sendEscapedHtmlChunk(field.help);
                sendChunk("</p>");
            }
            sendChunk("</div>");
        }
        sendChunk("</section>");
    }
    sendChunk("<div id='acbox' class='confirmbox' aria-hidden='true'><h2>Confirm changes</h2><p class='reviewhint'>Review the changed values before saving.</p><div class='tablewrap'><table><thead><tr><th>Parameter</th><th>Old</th><th>New</th></tr></thead><tbody id='acrows'></tbody></table></div><div class='actions'><input type='button' value='Confirm Save' onclick='acSubmit(this.form)'> <input class='cancelbtn' type='button' value='Cancel' onclick='acCancel(this.form)'></div></div><div id='acsavebar' class='actions acsavebar'><input type='submit' value='Save App Config'></div></form></div>");
    Esp32BaseWeb::sendFooter();
}

bool validateAllSubmitted(char* error, size_t errorLen) {
    char submitted[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
    char normalized[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
    for (uint8_t i = 0; i < g_appConfigFieldCount; ++i) {
        const AppConfigFieldSlot& field = g_appConfigFields[i];
        if (!getSubmittedRaw(field, i, submitted, sizeof(submitted))) {
            snprintf(error, errorLen, "%s is missing.", field.label);
            return false;
        }
        int32_t raw = 0;
        bool boolean = false;
        if (!validateSubmittedField(field, submitted, normalized, sizeof(normalized), raw, boolean, error, errorLen)) {
            return false;
        }
    }
    if (g_appConfigPageValidate) {
        g_appConfigSubmitContext = true;
        const bool ok = g_appConfigPageValidate(error, errorLen);
        g_appConfigSubmitContext = false;
        if (!ok) {
            if (!error[0]) {
                strlcpy(error, "App Config is invalid.", errorLen);
            }
            return false;
        }
    }
    return true;
}

bool writeSubmittedField(const AppConfigFieldSlot& field, uint8_t index, Esp32BaseAppConfig::SaveSummary& summary) {
    char submitted[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
    char normalized[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
    char oldText[Esp32BaseAppConfig::STRING_MAX_LENGTH + 1];
    char error[128] = "";
    int32_t newRaw = 0;
    int32_t oldRaw = 0;
    bool newBool = false;
    bool oldBool = false;
    getSubmittedRaw(field, index, submitted, sizeof(submitted));
    validateSubmittedField(field, submitted, normalized, sizeof(normalized), newRaw, newBool, error, sizeof(error));
    readAppConfigValue(field, oldText, sizeof(oldText), oldRaw, oldBool);
    bool changed = false;
    bool ok = true;
    switch (field.type) {
        case APP_CFG_STRING:
        case APP_CFG_ENUM:
            changed = strcmp(oldText, normalized) != 0;
            if (changed) {
                ok = Esp32BaseConfig::setStr(field.ns, field.key, normalized);
            }
            break;
        case APP_CFG_INT:
        case APP_CFG_DECIMAL:
            changed = oldRaw != newRaw;
            if (changed) {
                ok = Esp32BaseConfig::setInt(field.ns, field.key, newRaw);
            }
            break;
        case APP_CFG_BOOL:
            changed = oldBool != newBool;
            if (changed) {
                ok = Esp32BaseConfig::setBool(field.ns, field.key, newBool);
            }
            break;
    }
    if (!changed) {
        return true;
    }
    summary.changedCount++;
    if (!ok) {
        summary.failedCount++;
        return false;
    }
    summary.savedCount++;
    summary.restartRequired = summary.restartRequired || field.restartRequired;
    updateAppConfigPendingRestart(field, index, oldText, oldRaw, oldBool, normalized, newRaw, newBool);
    if (g_appConfigChangeCallback) {
        Esp32BaseAppConfig::Change change;
        change.field = makeFieldRef(field);
        change.oldValue = makeFieldValue(field, oldText, oldRaw, oldBool);
        change.newValue = makeFieldValue(field, normalized, newRaw, newBool);
        g_appConfigChangeCallback(change);
    }
    return true;
}

void handleAppConfigSubmit() {
    markRequest();
    if (!ensurePostAllowed("app_config")) {
        return;
    }
    int32_t rev = 0;
    if (!g_server.hasArg("rev") || !parseStrictInt32(g_server.arg("rev").c_str(), rev) || static_cast<uint32_t>(rev) != g_appConfigRevision) {
        redirectSeeOther("/esp32base/app-config?stale=1");
        return;
    }
    char error[128] = "";
    if (!validateAllSubmitted(error, sizeof(error))) {
        sendAppConfigPage(error);
        return;
    }
    Esp32BaseAppConfig::SaveSummary summary = {};
    for (uint8_t i = 0; i < g_appConfigFieldCount; ++i) {
        writeSubmittedField(g_appConfigFields[i], i, summary);
    }
    if (summary.savedCount > 0) {
        g_appConfigRevision++;
    }
    if (g_appConfigSaveCallback) {
        g_appConfigSaveCallback(summary);
    }
    if (summary.failedCount > 0) {
        redirectSeeOther("/esp32base/app-config?partial=1");
    } else if (summary.restartRequired) {
        redirectSeeOther("/esp32base/app-config?restart=1");
    } else {
        redirectSeeOther(summary.savedCount > 0 ? "/esp32base/app-config?saved=1" : "/esp32base/app-config");
    }
}

void handleAppConfigPage() {
    sendAppConfigPage();
}
#endif

} // namespace esp32base_web

using namespace esp32base_web;

#if ESP32BASE_ENABLE_APP_CONFIG
bool Esp32BaseAppConfig::setTitle(const char* title) {
    if (!title || !title[0] || strlen(title) >= sizeof(g_appConfigTitle)) {
        return false;
    }
    strlcpy(g_appConfigTitle, title, sizeof(g_appConfigTitle));
    return true;
}

bool Esp32BaseAppConfig::setPageValidateCallback(PageValidateCallback callback) {
    g_appConfigPageValidate = callback;
    return true;
}

bool Esp32BaseAppConfig::setChangeCallback(ChangeCallback callback) {
    g_appConfigChangeCallback = callback;
    return true;
}

bool Esp32BaseAppConfig::setSaveCallback(SaveCallback callback) {
    g_appConfigSaveCallback = callback;
    return true;
}

bool Esp32BaseAppConfig::addGroup(const Group& group) {
    if (!group.id || !group.id[0] || strlen(group.id) > 15 || !group.title || !group.title[0] ||
        strlen(group.title) > LABEL_MAX_LENGTH ||
        g_appConfigGroupCount >= ESP32BASE_APP_CONFIG_MAX_GROUPS || groupExists(group.id)) {
        ESP32BASE_LOG_W("appcfg", "group_register_failed id=%s", group.id ? group.id : "-");
        return false;
    }
    AppConfigGroupSlot& slot = g_appConfigGroups[g_appConfigGroupCount++];
    slot.used = true;
    slot.id = group.id;
    slot.title = group.title;
    return true;
}

bool Esp32BaseAppConfig::addString(const StringField& field) {
    if (g_appConfigFieldCount >= ESP32BASE_APP_CONFIG_MAX_FIELDS ||
        !validFieldCommon(field.groupId, field.ns, field.key, field.label) ||
        !validOptionalHelp(field.help) ||
        field.minLength > field.maxLength || field.maxLength > STRING_MAX_LENGTH ||
        !field.defaultValue || strlen(field.defaultValue) < field.minLength || strlen(field.defaultValue) > field.maxLength) {
        appConfigLogRegisterFailed("string", field.ns, field.key);
        return false;
    }
    AppConfigFieldSlot& slot = g_appConfigFields[g_appConfigFieldCount++];
    memset(&slot, 0, sizeof(slot));
    slot.used = true;
    slot.type = APP_CFG_STRING;
    slot.groupId = field.groupId;
    slot.ns = field.ns;
    slot.key = field.key;
    slot.label = field.label;
    slot.help = field.help;
    slot.restartRequired = field.restartRequired;
    slot.validate = field.validate;
    slot.spec.stringField = field;
    return true;
}

bool Esp32BaseAppConfig::addInt(const IntField& field) {
    if (g_appConfigFieldCount >= ESP32BASE_APP_CONFIG_MAX_FIELDS ||
        !validFieldCommon(field.groupId, field.ns, field.key, field.label) ||
        !validOptionalHelp(field.help) || !validOptionalUnit(field.unit) ||
        field.minValue > field.maxValue || field.defaultValue < field.minValue || field.defaultValue > field.maxValue ||
        field.step <= 0 || !stepMatches(field.defaultValue, field.minValue, field.step)) {
        appConfigLogRegisterFailed("int", field.ns, field.key);
        return false;
    }
    AppConfigFieldSlot& slot = g_appConfigFields[g_appConfigFieldCount++];
    memset(&slot, 0, sizeof(slot));
    slot.used = true;
    slot.type = APP_CFG_INT;
    slot.groupId = field.groupId;
    slot.ns = field.ns;
    slot.key = field.key;
    slot.label = field.label;
    slot.help = field.help;
    slot.unit = field.unit;
    slot.restartRequired = field.restartRequired;
    slot.validate = field.validate;
    slot.spec.intField = field;
    return true;
}

bool Esp32BaseAppConfig::addDecimal(const DecimalField& field) {
    if (g_appConfigFieldCount >= ESP32BASE_APP_CONFIG_MAX_FIELDS ||
        !validFieldCommon(field.groupId, field.ns, field.key, field.label) ||
        !validOptionalHelp(field.help) || !validOptionalUnit(field.unit) ||
        field.scale > DECIMAL_SCALE_MAX || field.minRawValue > field.maxRawValue ||
        field.defaultRawValue < field.minRawValue || field.defaultRawValue > field.maxRawValue ||
        field.stepRaw <= 0 || !stepMatches(field.defaultRawValue, field.minRawValue, field.stepRaw)) {
        appConfigLogRegisterFailed("decimal", field.ns, field.key);
        return false;
    }
    AppConfigFieldSlot& slot = g_appConfigFields[g_appConfigFieldCount++];
    memset(&slot, 0, sizeof(slot));
    slot.used = true;
    slot.type = APP_CFG_DECIMAL;
    slot.groupId = field.groupId;
    slot.ns = field.ns;
    slot.key = field.key;
    slot.label = field.label;
    slot.help = field.help;
    slot.unit = field.unit;
    slot.restartRequired = field.restartRequired;
    slot.validate = field.validate;
    slot.spec.decimalField = field;
    return true;
}

bool Esp32BaseAppConfig::addBool(const BoolField& field) {
    if (g_appConfigFieldCount >= ESP32BASE_APP_CONFIG_MAX_FIELDS ||
        !validFieldCommon(field.groupId, field.ns, field.key, field.label) ||
        !validOptionalHelp(field.help)) {
        appConfigLogRegisterFailed("bool", field.ns, field.key);
        return false;
    }
    AppConfigFieldSlot& slot = g_appConfigFields[g_appConfigFieldCount++];
    memset(&slot, 0, sizeof(slot));
    slot.used = true;
    slot.type = APP_CFG_BOOL;
    slot.groupId = field.groupId;
    slot.ns = field.ns;
    slot.key = field.key;
    slot.label = field.label;
    slot.help = field.help;
    slot.restartRequired = field.restartRequired;
    slot.validate = field.validate;
    slot.spec.boolField = field;
    return true;
}

bool Esp32BaseAppConfig::addEnum(const EnumField& field) {
    if (g_appConfigFieldCount >= ESP32BASE_APP_CONFIG_MAX_FIELDS ||
        !validFieldCommon(field.groupId, field.ns, field.key, field.label) ||
        !validOptionalHelp(field.help) || !validEnumOptions(field)) {
        appConfigLogRegisterFailed("enum", field.ns, field.key);
        return false;
    }
    AppConfigFieldSlot& slot = g_appConfigFields[g_appConfigFieldCount++];
    memset(&slot, 0, sizeof(slot));
    slot.used = true;
    slot.type = APP_CFG_ENUM;
    slot.groupId = field.groupId;
    slot.ns = field.ns;
    slot.key = field.key;
    slot.label = field.label;
    slot.help = field.help;
    slot.restartRequired = field.restartRequired;
    slot.validate = field.validate;
    slot.spec.enumField = field;
    return true;
}

bool Esp32BaseAppConfig::submittedString(const char* ns, const char* key, char* out, size_t len) {
    const AppConfigFieldSlot* field = nullptr;
    return g_appConfigSubmitContext && findSubmittedRaw(ns, key, out, len, &field) && field && field->type == APP_CFG_STRING;
}

bool Esp32BaseAppConfig::submittedInt(const char* ns, const char* key, int32_t& out) {
    char raw[32];
    const AppConfigFieldSlot* field = nullptr;
    return g_appConfigSubmitContext && findSubmittedRaw(ns, key, raw, sizeof(raw), &field) && field && field->type == APP_CFG_INT && parseStrictInt32(raw, out);
}

bool Esp32BaseAppConfig::submittedDecimal(const char* ns, const char* key, int32_t& rawOut) {
    char raw[32];
    const AppConfigFieldSlot* field = nullptr;
    return g_appConfigSubmitContext && findSubmittedRaw(ns, key, raw, sizeof(raw), &field) && field && field->type == APP_CFG_DECIMAL &&
           parseDecimalRaw(raw, field->spec.decimalField.scale, rawOut);
}

bool Esp32BaseAppConfig::submittedBool(const char* ns, const char* key, bool& out) {
    char raw[4];
    const AppConfigFieldSlot* field = nullptr;
    if (!(g_appConfigSubmitContext && findSubmittedRaw(ns, key, raw, sizeof(raw), &field) && field && field->type == APP_CFG_BOOL)) {
        return false;
    }
    out = strcmp(raw, "1") == 0;
    return true;
}

bool Esp32BaseAppConfig::submittedEnum(const char* ns, const char* key, char* out, size_t len) {
    const AppConfigFieldSlot* field = nullptr;
    return g_appConfigSubmitContext && findSubmittedRaw(ns, key, out, len, &field) && field && field->type == APP_CFG_ENUM;
}
#endif

#endif
