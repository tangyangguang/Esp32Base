#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "WebInternal.h"

namespace esp32base_web {

#if ESP32BASE_ENABLE_WATCHDOG
WatchdogTripState readWatchdogTripState() {
    WatchdogTripState state = {};
    state.lifetime = Esp32BaseWatchdog::lifetimeResetCount();
    const int32_t base = Esp32BaseConfig::getInt("eb_sys", "wdt_trip_base", -1);
    state.hasBase = base >= 0;
    state.base = state.hasBase ? static_cast<uint32_t>(base) : 0;
    state.resetTime = static_cast<uint32_t>(Esp32BaseConfig::getInt("eb_sys", "wdt_trip_time", 0));
    state.invalidBase = state.hasBase && state.base > state.lifetime;
    state.trip = (!state.invalidBase && state.lifetime >= state.base) ? state.lifetime - state.base : 0;
    return state;
}

bool formatEpochTime(uint32_t epoch, char* out, size_t len) {
    if (!out || len == 0 || epoch == 0) {
        return false;
    }
    const time_t raw = static_cast<time_t>(epoch);
    struct tm tmValue;
    localtime_r(&raw, &tmValue);
    return strftime(out, len, "%Y-%m-%d %H:%M:%S", &tmValue) > 0;
}

void formatWatchdogTripResetAt(const WatchdogTripState& state, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (!state.hasBase) {
        strlcpy(out, "never", len);
        return;
    }
    if (state.invalidBase) {
        strlcpy(out, "invalid baseline", len);
        return;
    }
#if ESP32BASE_ENABLE_NTP
    if (state.resetTime >= ESP32BASE_NTP_SYNC_MIN_EPOCH && formatEpochTime(state.resetTime, out, len)) {
        return;
    }
#endif
    strlcpy(out, "unknown (time unavailable)", len);
}

uint32_t currentWatchdogTripResetTime() {
#if ESP32BASE_ENABLE_NTP
    const Esp32BaseNtp::TimeSnapshot time = Esp32BaseNtp::snapshot();
    if (time.synced) {
        return time.epochSec;
    }
#endif
    return 0;
}
#endif

void formatMac(uint64_t mac, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    snprintf(out, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             static_cast<unsigned>(mac & 0xff),
             static_cast<unsigned>((mac >> 8) & 0xff),
             static_cast<unsigned>((mac >> 16) & 0xff),
             static_cast<unsigned>((mac >> 24) & 0xff),
             static_cast<unsigned>((mac >> 32) & 0xff),
             static_cast<unsigned>((mac >> 40) & 0xff));
}

const char* partitionTypeName(esp_partition_type_t type) {
    switch (type) {
        case ESP_PARTITION_TYPE_APP: return "app";
        case ESP_PARTITION_TYPE_DATA: return "data";
        default: return "unknown";
    }
}

const char* partitionSubtypeName(const esp_partition_t* partition) {
    if (!partition) {
        return "unknown";
    }
    if (partition->type == ESP_PARTITION_TYPE_APP) {
        if (partition->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
            partition->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
            return partition->label;
        }
        switch (partition->subtype) {
            case ESP_PARTITION_SUBTYPE_APP_FACTORY: return "factory";
            case ESP_PARTITION_SUBTYPE_APP_TEST: return "test";
            default: return "app";
        }
    }
    if (partition->type == ESP_PARTITION_TYPE_DATA) {
        switch (partition->subtype) {
            case ESP_PARTITION_SUBTYPE_DATA_NVS: return "nvs";
            case ESP_PARTITION_SUBTYPE_DATA_PHY: return "phy";
            case ESP_PARTITION_SUBTYPE_DATA_OTA: return "ota";
            case ESP_PARTITION_SUBTYPE_DATA_SPIFFS: return "spiffs";
            case ESP_PARTITION_SUBTYPE_DATA_COREDUMP: return "coredump";
            default: return "data";
        }
    }
    return "unknown";
}

bool samePartition(const esp_partition_t* a, const esp_partition_t* b) {
    return a && b && a->address == b->address && a->size == b->size &&
           a->type == b->type && a->subtype == b->subtype &&
           strncmp(a->label, b->label, sizeof(a->label)) == 0;
}

void webBytesToHex(const uint8_t* bytes, size_t len, char* out, size_t outLen) {
    static const char* hex = "0123456789abcdef";
    if (!out || outLen < len * 2 + 1) {
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = hex[(bytes[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

const char* otaImageStateName(const esp_partition_t* partition) {
    if (!partition || partition->type != ESP_PARTITION_TYPE_APP ||
        partition->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MIN ||
        partition->subtype > ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
        return "n/a";
    }
    esp_ota_img_states_t state;
    const esp_err_t err = esp_ota_get_state_partition(partition, &state);
    if (err == ESP_ERR_NOT_FOUND) {
        return "undefined";
    }
    if (err != ESP_OK) {
        return "unknown";
    }
    switch (state) {
        case ESP_OTA_IMG_NEW: return "new";
        case ESP_OTA_IMG_PENDING_VERIFY: return "pending_verify";
        case ESP_OTA_IMG_VALID: return "valid";
        case ESP_OTA_IMG_INVALID: return "invalid";
        case ESP_OTA_IMG_ABORTED: return "aborted";
        case ESP_OTA_IMG_UNDEFINED: return "undefined";
        default: return "unknown";
    }
}

void partitionImageSha256(const esp_partition_t* partition, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    out[0] = '\0';
    if (!partition || partition->type != ESP_PARTITION_TYPE_APP) {
        return;
    }
    uint8_t digest[32];
    if (esp_partition_get_sha256(partition, digest) == ESP_OK) {
        webBytesToHex(digest, sizeof(digest), out, len);
    }
}

void partitionAppVersion(const esp_partition_t* partition, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    out[0] = '\0';
    if (!partition || partition->type != ESP_PARTITION_TYPE_APP) {
        return;
    }
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(partition, &desc) == ESP_OK) {
        strlcpy(out, desc.version, len);
    }
}

void sendPartitionJson(const char* key, const esp_partition_t* partition) {
    char sha[65] = "";
    char version[33] = "";
    sendChunk("\"");
    sendEscapedJsonChunk(key);
    sendChunk("\":");
    if (!partition) {
        sendChunk("null");
        return;
    }
    partitionImageSha256(partition, sha, sizeof(sha));
    partitionAppVersion(partition, version, sizeof(version));
    sendChunk("{\"label\":\"");
    sendEscapedJsonChunk(partition->label);
    sendChunk("\",\"address\":");
    sendIntChunk(static_cast<int>(partition->address));
    sendChunk(",\"size\":");
    sendBytesJsonChunk(partition->size);
    sendChunk(",\"state\":\"");
    sendEscapedJsonChunk(otaImageStateName(partition));
    sendChunk("\",\"sha256\":\"");
    sendEscapedJsonChunk(sha);
    sendChunk("\",\"version\":\"");
    sendEscapedJsonChunk(version);
    sendChunk("\"}");
}

void sendAppPartitionsJson() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    const esp_partition_t* nextOta = esp_ota_get_next_update_partition(nullptr);
    bool first = true;
    sendChunk("\"appPartitions\":[");
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    while (it) {
        const esp_partition_t* p = esp_partition_get(it);
        if (p) {
            char sha[65] = "";
            char version[33] = "";
            partitionImageSha256(p, sha, sizeof(sha));
            partitionAppVersion(p, version, sizeof(version));
            if (!first) {
                sendChunk(",");
            }
            first = false;
            sendChunk("{\"label\":\"");
            sendEscapedJsonChunk(p->label);
            sendChunk("\",\"subtype\":\"");
            sendEscapedJsonChunk(partitionSubtypeName(p));
            sendChunk("\",\"address\":");
            sendIntChunk(static_cast<int>(p->address));
            sendChunk(",\"size\":");
            sendBytesJsonChunk(p->size);
            sendChunk(",\"running\":");
            sendChunk(samePartition(p, running) ? "true" : "false");
            sendChunk(",\"boot\":");
            sendChunk(samePartition(p, boot) ? "true" : "false");
            sendChunk(",\"nextUpdate\":");
            sendChunk(samePartition(p, nextOta) ? "true" : "false");
            sendChunk(",\"state\":\"");
            sendEscapedJsonChunk(otaImageStateName(p));
            sendChunk("\",\"sha256\":\"");
            sendEscapedJsonChunk(sha);
            sendChunk("\",\"version\":\"");
            sendEscapedJsonChunk(version);
            sendChunk("\"}");
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    sendChunk("]");
}

const char* partitionRole(const esp_partition_t* partition, const esp_partition_t* running, const esp_partition_t* boot, const esp_partition_t* nextOta) {
    if (!partition) {
        return "-";
    }
    if (samePartition(partition, running) && samePartition(partition, boot)) {
        return "running app / boot";
    }
    if (samePartition(partition, running)) {
        return "running app";
    }
    if (samePartition(partition, boot)) {
        return "boot app";
    }
    if (samePartition(partition, nextOta)) {
        return "next OTA";
    }
    if (partition->type == ESP_PARTITION_TYPE_APP) {
        return "app";
    }
    if (partition->type == ESP_PARTITION_TYPE_DATA) {
        switch (partition->subtype) {
            case ESP_PARTITION_SUBTYPE_DATA_NVS: return "NVS config";
            case ESP_PARTITION_SUBTYPE_DATA_OTA: return "OTA state";
            case ESP_PARTITION_SUBTYPE_DATA_SPIFFS: return "app data";
            case ESP_PARTITION_SUBTYPE_DATA_COREDUMP: return "coredump";
            default: return "data";
        }
    }
    return "-";
}

bool statusDetailsMode() {
    return g_server.hasArg("details") && g_server.arg("details") != "0";
}

void sendPartitionTable() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    const esp_partition_t* nextOta = esp_ota_get_next_update_partition(nullptr);
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    sendChunk("<section class='panel statuspage'><h2>Partition Table</h2><div class='tablewrap'><table class='part'><tr><th>Name</th><th>Type</th><th>SubType</th><th>Offset</th><th>Size</th><th>Role</th></tr>");
    while (it) {
        const esp_partition_t* p = esp_partition_get(it);
        if (p) {
            char offset[16];
            char sizeBuf[48];
            snprintf(offset, sizeof(offset), "0x%06lx", static_cast<unsigned long>(p->address));
            formatReadableBytes(p->size, sizeBuf, sizeof(sizeBuf));
            sendChunk("<tr><td>");
            sendEscapedHtmlChunk(p->label);
            sendChunk("</td><td>");
            sendEscapedHtmlChunk(partitionTypeName(p->type));
            sendChunk("</td><td>");
            sendEscapedHtmlChunk(partitionSubtypeName(p));
            sendChunk("</td><td>");
            sendEscapedHtmlChunk(offset);
            sendChunk("</td><td>");
            sendEscapedHtmlChunk(sizeBuf);
            sendChunk("</td><td>");
            sendEscapedHtmlChunk(partitionRole(p, running, boot, nextOta));
            sendChunk("</td></tr>");
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    sendChunk("</table></div></section>");
}

void sendFirmwareOtaDetails() {
    char runningElfSha[65] = "";
    esp_ota_get_app_elf_sha256(runningElfSha, sizeof(runningElfSha));
    sendInfoRow("Running ELF SHA256", runningElfSha[0] ? runningElfSha : "unavailable");
}

void handleStatus() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    if (!beginResponse(200, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"firmware\":{\"name\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareName());
    sendChunk("\",\"version\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareVersion());
    sendChunk("\",\"build\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareBuild());
    sendChunk("\"},\"profile\":\"");
    sendEscapedJsonChunk(Esp32Base::profileName());
    sendChunk("\",\"hostname\":\"");
    sendEscapedJsonChunk(Esp32Base::hostname());
    sendChunk("\",\"heap\":{\"free\":");
    sendBytesJsonChunk(Esp32BaseSystem::freeHeap());
    sendChunk(",\"minFree\":");
    sendBytesJsonChunk(Esp32BaseSystem::minFreeHeap());
    sendChunk("},\"flash\":");
    sendBytesJsonChunk(Esp32BaseSystem::flashSize());
    sendChunk(",\"resetReason\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::resetReason());
    sendChunk("\",\"resetReasonText\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::resetReasonText());
    sendChunk("\",\"wakeReason\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::wakeReason());
    sendChunk("\",\"wakeReasonText\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::wakeReasonText());
    sendChunk("\"");
    sendChunk(",\"wifi\":{\"state\":\"");
    sendEscapedJsonChunk(Esp32BaseWiFi::stateName());
    sendChunk("\",\"connected\":");
    sendChunk(Esp32BaseWiFi::isConnected() ? "true" : "false");
    sendChunk(",\"rssi\":");
    sendIntChunk(Esp32BaseWiFi::isConnected() ? Esp32BaseWiFi::rssi() : 0);
    sendChunk("}}");
    endResponse();
}

void handleChip() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    if (!beginResponse(200, "application/json", nullptr)) {
        return;
    }
    char mac[18];
    formatMac(ESP.getEfuseMac(), mac, sizeof(mac));
    sendChunk("{\"chipModel\":\"");
    sendEscapedJsonChunk(ESP.getChipModel());
    sendChunk("\",\"chipRevision\":");
    sendIntChunk(ESP.getChipRevision());
    sendChunk(",\"cpuMHz\":");
    sendIntChunk(ESP.getCpuFreqMHz());
    sendChunk(",\"sdkVersion\":\"");
    sendEscapedJsonChunk(ESP.getSdkVersion());
    sendChunk("\",\"efuseMac\":\"");
    sendEscapedJsonChunk(mac);
    sendChunk("\",\"staMac\":\"");
    sendEscapedJsonChunk(WiFi.macAddress().c_str());
    sendChunk("\",\"apMac\":\"");
    sendEscapedJsonChunk(WiFi.softAPmacAddress().c_str());
    sendChunk("\",\"freeHeap\":");
    sendBytesJsonChunk(Esp32BaseSystem::freeHeap());
    sendChunk(",\"minFreeHeap\":");
    sendBytesJsonChunk(Esp32BaseSystem::minFreeHeap());
    sendChunk(",\"totalHeap\":");
    sendBytesJsonChunk(Esp32BaseSystem::totalHeap());
    sendChunk(",\"psram\":{\"total\":");
    sendBytesJsonChunk(ESP.getPsramSize());
    sendChunk(",\"free\":");
    sendBytesJsonChunk(ESP.getFreePsram());
    sendChunk("}");
    sendChunk(",\"flash\":");
    sendBytesJsonChunk(Esp32BaseSystem::flashSize());
    sendChunk(",\"resetReason\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::resetReason());
    sendChunk("\",\"resetReasonText\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::resetReasonText());
    sendChunk("\",\"wakeReason\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::wakeReason());
    sendChunk("\",\"wakeReasonText\":\"");
    sendEscapedJsonChunk(Esp32BaseSystem::wakeReasonText());
    sendChunk("\"}");
    endResponse();
}

void handleFirmware() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    if (!beginResponse(200, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"name\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareName());
    sendChunk("\",\"version\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareVersion());
    sendChunk("\",\"build\":\"");
    sendEscapedJsonChunk(Esp32Base::firmwareBuild());
    sendChunk("\"}");
    endResponse();
}

bool loadStoredHostname(char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    out[0] = '\0';
    return Esp32BaseConfig::getStr("eb_sys", "hostname", out, len, "");
}

bool hostnameRestartRequired(const char* storedHostname) {
    return storedHostname && storedHostname[0] &&
           Esp32Base::isValidHostname(storedHostname) &&
           strcmp(storedHostname, Esp32Base::hostname()) != 0;
}

void sendHostnameJson(int code) {
    char stored[64] = "";
    const bool hasStored = loadStoredHostname(stored, sizeof(stored));
    const bool storedValid = hasStored && Esp32Base::isValidHostname(stored);
    if (!beginResponse(code, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"currentHostname\":\"");
    sendEscapedJsonChunk(Esp32Base::hostname());
    sendChunk("\",\"defaultHostname\":\"");
    sendEscapedJsonChunk(Esp32Base::defaultHostname());
    sendChunk("\",\"storedHostname\":\"");
    sendEscapedJsonChunk(hasStored ? stored : "");
    sendChunk("\",\"storedValid\":");
    sendChunk(storedValid ? "true" : "false");
    sendChunk(",\"restartRequired\":");
    sendChunk(hostnameRestartRequired(stored) ? "true" : "false");
    sendChunk(",\"rule\":\"1-32 lowercase letters, digits and hyphen; no leading/trailing hyphen; no .local\"}");
    endResponse();
}

void handleHostnameApiGet() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    sendHostnameJson(200);
}

void handleHostnameSubmit() {
    markRequest();
    if (!ensurePostAllowed("hostname_submit")) {
        return;
    }
    const String hostname = g_server.arg("hostname");
    const String uri = g_server.uri();
    const bool apiRequest = strcmp(uri.c_str(), "/esp32base/api/hostname") == 0;
    if (!Esp32Base::isValidHostname(hostname.c_str())) {
        ESP32BASE_LOG_W("web", "hostname_save_rejected value=%s", hostname.c_str());
        if (apiRequest) {
            sendHostnameJson(400);
        } else {
            redirectSeeOther("/esp32base/tools?hostname_error=invalid");
        }
        return;
    }
    const bool ok = Esp32BaseConfig::setStr("eb_sys", "hostname", hostname.c_str());
    ESP32BASE_LOG_I("web", "hostname_save value=%s current=%s restart_required=%s result=%s",
                    hostname.c_str(),
                    Esp32Base::hostname(),
                    strcmp(hostname.c_str(), Esp32Base::hostname()) != 0 ? "yes" : "no",
                    ok ? "success" : "failed");
    if (apiRequest) {
        sendHostnameJson(ok ? 200 : 500);
    } else {
        redirectSeeOther(ok ? "/esp32base/tools?hostname_saved=1" : "/esp32base/tools?hostname_error=save_failed");
    }
}

void handleRoot() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    if (useAppHome()) {
        redirectSeeOther(configuredHomePath());
        return;
    }
    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_HOME]);
    Esp32BaseWeb::sendPageTitle(g_deviceName);

    char value[160];
    char uptime[32];
    char freeHeap[48];
    char minHeap[48];
    char maxAllocHeap[48];
    char totalHeap[48];
    char otaTargetSize[48] = "unavailable";
    char runningSlot[80] = "unknown";
    char bootSlot[80] = "unknown";
    char otaTargetSlot[80] = "unavailable";
    char flash[48];
    const bool details = statusDetailsMode();
    Esp32BaseLog::formatUptime(Esp32BaseSystem::uptimeMs(), uptime, sizeof(uptime));
    formatReadableBytes(Esp32BaseSystem::freeHeap(), freeHeap, sizeof(freeHeap));
    formatReadableBytes(Esp32BaseSystem::minFreeHeap(), minHeap, sizeof(minHeap));
    formatReadableBytes(ESP.getMaxAllocHeap(), maxAllocHeap, sizeof(maxAllocHeap));
    formatReadableBytes(Esp32BaseSystem::totalHeap(), totalHeap, sizeof(totalHeap));
    formatReadableBytes(Esp32BaseSystem::flashSize(), flash, sizeof(flash));
    const esp_partition_t* runningPartition = esp_ota_get_running_partition();
    const esp_partition_t* bootPartition = esp_ota_get_boot_partition();
    const esp_partition_t* nextPartition = esp_ota_get_next_update_partition(nullptr);
    if (runningPartition) {
        char runningSize[48];
        formatReadableBytes(runningPartition->size, runningSize, sizeof(runningSize));
        snprintf(runningSlot, sizeof(runningSlot), "%s / %s", runningPartition->label, runningSize);
    }
    if (bootPartition) {
        char bootSize[48];
        formatReadableBytes(bootPartition->size, bootSize, sizeof(bootSize));
        snprintf(bootSlot, sizeof(bootSlot), "%s / %s", bootPartition->label, bootSize);
    }
    if (nextPartition) {
        formatReadableBytes(nextPartition->size, otaTargetSize, sizeof(otaTargetSize));
        snprintf(otaTargetSlot, sizeof(otaTargetSlot), "%s / %s", nextPartition->label, otaTargetSize);
    }
    sendChunk("<div class='statusgrid'>");

    sendStatusSectionStart("Device");
    sendInfoRow("Name", g_deviceName);
    sendInfoRow("Hostname", Esp32Base::hostname());
    sendInfoRowStart("Firmware");
    sendEscapedHtmlChunk(Esp32Base::firmwareName());
    sendChunk(" ");
    sendEscapedHtmlChunk(Esp32Base::firmwareVersion());
    if (Esp32Base::firmwareBuild()[0]) {
        sendChunk(" <span class='info'>");
        sendEscapedHtmlChunk(Esp32Base::firmwareBuild());
        sendChunk("</span>");
    }
    sendInfoRowEnd();
    sendInfoRow("Profile", Esp32Base::profileName());
    sendInfoRow("Uptime", uptime);
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(Esp32BaseSystem::bootCount()));
    sendInfoRow("Boot count", value);
    sendStatusSectionEnd();

#if ESP32BASE_ENABLE_WIFI
    sendStatusSectionStart("Network");
    sendInfoRowStart("WiFi");
    sendEscapedHtmlChunk(Esp32BaseWiFi::stateName());
    if (Esp32BaseWiFi::ssid()[0]) {
        sendChunk(" / ");
        sendEscapedHtmlChunk(Esp32BaseWiFi::ssid());
    }
    sendInfoRowEnd();
    char ip[24] = "-";
    if (Esp32BaseWiFi::isConnected()) {
        Esp32BaseWiFi::ip(ip, sizeof(ip));
    }
    sendInfoRow("IP", ip);
    if (Esp32BaseWiFi::isConnected()) {
        snprintf(value, sizeof(value), "%ld dBm", static_cast<long>(Esp32BaseWiFi::rssi()));
    } else {
        strlcpy(value, "-", sizeof(value));
    }
    sendInfoRow("RSSI", value);
    sendInfoRow("Power save", Esp32BaseWiFi::powerSave() ? "on" : "off");
    sendInfoRow("STA MAC", WiFi.macAddress().c_str());
    sendInfoRow("AP MAC", WiFi.softAPmacAddress().c_str());
    sendStatusSectionEnd();
#endif

    sendStatusSectionStart("Runtime Health");
    sendInfoRowStart("Heap");
    sendSubmetricsStart();
    sendSubmetric("Free", freeHeap);
    sendSubmetric("Min", minHeap);
    sendSubmetric("Max alloc", maxAllocHeap);
    sendSubmetric("Total", totalHeap);
    sendSubmetricsEnd();
    sendInfoRowEnd();
#if ESP32BASE_ENABLE_WATCHDOG
    const WatchdogTripState watchdogTrip = readWatchdogTripState();
    char tripResetAt[32];
    formatWatchdogTripResetAt(watchdogTrip, tripResetAt, sizeof(tripResetAt));
    sendInfoRowStart("Watchdog");
    sendStatusTag(Esp32BaseWeb::UI_OK, "enabled");
    sendSubmetricsStart();
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(watchdogTrip.lifetime));
    sendSubmetric("Lifetime resets", value);
    if (watchdogTrip.invalidBase) {
        strlcpy(value, "invalid baseline", sizeof(value));
    } else {
        snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(watchdogTrip.trip));
    }
    sendSubmetric("Trip resets", value);
    sendSubmetric("Trip reset at", tripResetAt);
    sendSubmetricsEnd();
    sendInfoRowEnd();
#endif
#if ESP32BASE_ENABLE_NTP
    const Esp32BaseNtp::TimeSnapshot time = Esp32BaseNtp::snapshot();
    sendInfoRowStart("NTP time");
    if (!Esp32BaseNtp::isStarted()) {
        sendStatusTag(Esp32BaseWeb::UI_NEUTRAL, "not started");
    } else if (time.synced) {
        sendStatusTag(Esp32BaseWeb::UI_OK, "synced");
    } else {
        sendStatusTag(Esp32BaseWeb::UI_WARN, "pending");
    }
    if (time.synced && Esp32BaseNtp::formatTime(value, sizeof(value), "%Y-%m-%d %H:%M:%S")) {
        sendChunk("<br>");
        sendEscapedHtmlChunk(value);
    }
    sendInfoRowEnd();
#endif
    sendInfoRowStart("Last reset");
    sendEscapedHtmlChunk(Esp32BaseSystem::resetReason());
    sendChunk(" / ");
    sendEscapedHtmlChunk(Esp32BaseSystem::resetReasonText());
    sendInfoRowEnd();
    sendInfoRowStart("Last wake");
    sendEscapedHtmlChunk(Esp32BaseSystem::wakeReason());
    sendChunk(" / ");
    sendEscapedHtmlChunk(Esp32BaseSystem::wakeReasonText());
    sendInfoRowEnd();
    sendStatusSectionEnd();

#if ESP32BASE_ENABLE_FS || ESP32BASE_ENABLE_FILELOG
    sendStatusSectionStart("Storage & Logs");
#if ESP32BASE_ENABLE_FS
    if (Esp32BaseFs::isReady()) {
        sendFsQuickSummaryRows();
    } else {
        sendTaggedInfoRow("FS", "unavailable", Esp32BaseWeb::UI_WARN);
    }
#endif
#if ESP32BASE_ENABLE_FILELOG
    sendInfoRowStart("System logs");
    sendFileLogRuntimeStateTag();
    if (fileLogHasRuntimeDetails()) {
        char logLimit[48];
        char rotateFiles[12];
        formatReadableBytes(static_cast<uint64_t>(Esp32BaseFileLog::maxBytes()) * Esp32BaseFileLog::rotateFiles(), logLimit, sizeof(logLimit));
        snprintf(rotateFiles, sizeof(rotateFiles), "%u", static_cast<unsigned>(Esp32BaseFileLog::rotateFiles()));
        sendSubmetricsStart();
        sendSubmetric("Log level", Esp32BaseFileLog::modeName());
        sendSubmetric("Files", rotateFiles);
        sendSubmetric("Limit", logLimit);
        sendSubmetricsEnd();
    }
    sendInfoRowEnd();
    if (fileLogHasRuntimeDetails()) {
        sendInfoRow("Log path", Esp32BaseFileLog::path());
    }
#endif
    sendStatusSectionEnd();
#endif

    sendStatusSectionStart("Firmware & OTA");
    sendInfoRow("Running slot", runningSlot);
    sendInfoRow("Boot slot", bootSlot);
    sendInfoRow("OTA target slot", otaTargetSlot);
    sendInfoRow("Max OTA upload", otaTargetSize);
#if ESP32BASE_ENABLE_OTA
    sendInfoRow("Rollback", Esp32BaseOta::isRollbackPossible() ? "possible" : "not possible");
    if (Esp32BaseOta::lastError()[0]) {
        sendInfoRow("Last OTA error", Esp32BaseOta::lastError());
    }
#endif
    sendInfoRowStart("OTA & partition details");
    sendChunk(details ? "<a class='btnlink secondary' href='/esp32base'>Hide OTA &amp; partition details</a>" : "<a class='btnlink info' href='/esp32base?details=1'>Show OTA &amp; partition details</a>");
    sendInfoRowEnd();
    if (details) {
        sendFirmwareOtaDetails();
    }
    sendStatusSectionEnd();

    char mac[18];
    sendStatusSectionStart("Hardware");
    sendInfoRow("Chip", ESP.getChipModel());
    snprintf(value, sizeof(value), "%d", ESP.getChipRevision());
    sendInfoRow("Revision", value);
    snprintf(value, sizeof(value), "%u MHz", static_cast<unsigned>(ESP.getCpuFreqMHz()));
    sendInfoRow("CPU", value);
    sendInfoRow("SDK", ESP.getSdkVersion());
    sendInfoRow("Flash chip", flash);
    if (ESP.getPsramSize() > 0) {
        char psramTotal[48];
        char psramFree[48];
        formatReadableBytes(ESP.getPsramSize(), psramTotal, sizeof(psramTotal));
        formatReadableBytes(ESP.getFreePsram(), psramFree, sizeof(psramFree));
        snprintf(value, sizeof(value), "free %s, total %s", psramFree, psramTotal);
        sendInfoRow("PSRAM", value);
    }
    formatMac(ESP.getEfuseMac(), mac, sizeof(mac));
    sendInfoRow("eFuse MAC", mac);
    sendStatusSectionEnd();
    sendChunk("</div>");

    if (g_homeMode == Esp32BaseWeb::HOME_ESP32BASE && appNavCount() > 0) {
        sendChunk("<section class='panel appsection'><h2>Application</h2>");
        sendAppLinks(true, nullptr);
        sendChunk("</section>");
    }

    if (details) {
        sendPartitionTable();
    }
    Esp32BaseWeb::sendFooter();
}

} // namespace esp32base_web

#endif
