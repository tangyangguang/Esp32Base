#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB

#include "WebInternal.h"
#include "../../update/internal/Esp32BaseOtaCompat.h"

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
#if ESP32BASE_ENABLE_TIME
    return Esp32BaseTime::formatEpoch(epoch, out, len, "%Y-%m-%d %H:%M:%S");
#else
    out[0] = '\0';
    return false;
#endif
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
#if ESP32BASE_ENABLE_TIME
    if (state.resetTime >= ESP32BASE_TIME_SYNC_MIN_EPOCH && formatEpochTime(state.resetTime, out, len)) {
        return;
    }
#endif
    strlcpy(out, "unknown (time unavailable)", len);
}

uint32_t currentWatchdogTripResetTime() {
#if ESP32BASE_ENABLE_TIME
    const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
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

enum class RunningImageSizeState : uint8_t {
    Unread,
    Available,
    Unavailable
};

RunningImageSizeState g_runningImageSizeState = RunningImageSizeState::Unread;
uint32_t g_runningImageSizeBytes = 0;

bool runningImageSize(uint32_t& out) {
    if (g_runningImageSizeState == RunningImageSizeState::Unread) {
        const esp_partition_t* running = esp_ota_get_running_partition();
        if (running) {
            const esp_partition_pos_t position = {
                .offset = running->address,
                .size = running->size,
            };
            esp_image_metadata_t metadata = {};
            if (esp_image_get_metadata(&position, &metadata) == ESP_OK &&
                metadata.image_len > 0 && metadata.image_len <= running->size) {
                g_runningImageSizeBytes = metadata.image_len;
                g_runningImageSizeState = RunningImageSizeState::Available;
            } else {
                g_runningImageSizeState = RunningImageSizeState::Unavailable;
            }
        } else {
            g_runningImageSizeState = RunningImageSizeState::Unavailable;
        }
    }
    out = g_runningImageSizeBytes;
    return g_runningImageSizeState == RunningImageSizeState::Available;
}

void sendStatusCardStart(const char* title,
                         Esp32BaseWeb::UiTone tone,
                         const char* state,
                         const char* extraClass = nullptr) {
    sendChunk("<section class='panel statuspage statuscard");
    if (extraClass && extraClass[0]) {
        sendChunk(" ");
        sendEscapedHtmlChunk(extraClass);
    }
    sendChunk("'><div class='statuscardhead'><h2>");
    sendEscapedHtmlChunk(title);
    sendChunk("</h2>");
    sendStatusTag(tone, state);
    sendChunk("</div><div class='tablewrap'><table class='kv'>");
}

bool resetReasonNeedsAttention() {
    switch (esp_reset_reason()) {
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
            return true;
        default:
            return false;
    }
}

#if ESP32BASE_ENABLE_OTA
const char* otaStatusName(Esp32BaseOta::Status status) {
    switch (status) {
        case Esp32BaseOta::IDLE: return "idle";
        case Esp32BaseOta::READY: return "ready";
        case Esp32BaseOta::UPLOADING: return "uploading";
        case Esp32BaseOta::VERIFYING: return "verifying";
        case Esp32BaseOta::SUCCESS: return "success";
        case Esp32BaseOta::FAILED: return "failed";
        default: return "unknown";
    }
}
#endif

void formatIpAddress(const IPAddress& address, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    snprintf(out, len, "%u.%u.%u.%u",
             static_cast<unsigned>(address[0]),
             static_cast<unsigned>(address[1]),
             static_cast<unsigned>(address[2]),
             static_cast<unsigned>(address[3]));
}

void sendFirmwareOtaDetails() {
    char runningElfSha[65] = "";
    esp32base_internal::appElfSha256(runningElfSha, sizeof(runningElfSha));
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
    sendChunk("\",\"imageSize\":");
    uint32_t imageSize = 0;
    if (runningImageSize(imageSize)) {
        sendBytesJsonChunk(imageSize);
    } else {
        sendChunk("null");
    }
    sendChunk("},\"profile\":\"");
    sendEscapedJsonChunk(Esp32Base::profileName());
    sendChunk("\",\"hostname\":\"");
    sendEscapedJsonChunk(Esp32Base::hostname());
    sendChunk("\",\"uptimeMs\":");
    sendUintChunk(Esp32BaseSystem::uptimeMs64());
    sendChunk(",\"heap\":{\"free\":");
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
    sendChunk("\",\"imageSize\":");
    uint32_t imageSize = 0;
    if (runningImageSize(imageSize)) {
        sendBytesJsonChunk(imageSize);
    } else {
        sendChunk("null");
    }
    sendChunk("}");
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
            redirectSeeOther("/esp32base/system?hostname_error=invalid");
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
        redirectSeeOther(ok ? "/esp32base/system?hostname_saved=1" : "/esp32base/system?hostname_error=save_failed");
    }
}

void handleStatusPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_HOME]);
    Esp32BaseWeb::sendPageTitle(g_deviceName, "Operational status and low-cost diagnostics");

    char value[160] = "";
    char uptime[32] = "";
    char freeHeap[48] = "";
    char minHeap[48] = "";
    char maxAllocHeap[48] = "";
    char totalHeap[48] = "";
    char imageSizeText[48] = "unavailable";
    char otaTargetSize[48] = "unavailable";
    char runningSlot[80] = "unknown";
    char bootSlot[80] = "unknown";
    char otaTargetSlot[80] = "unavailable";
    char flash[48] = "";
    Esp32BaseLog::formatUptime64(Esp32BaseSystem::uptimeMs64(), uptime, sizeof(uptime));
    formatReadableBytes(Esp32BaseSystem::freeHeap(), freeHeap, sizeof(freeHeap));
    formatReadableBytes(Esp32BaseSystem::minFreeHeap(), minHeap, sizeof(minHeap));
    formatReadableBytes(ESP.getMaxAllocHeap(), maxAllocHeap, sizeof(maxAllocHeap));
    formatReadableBytes(Esp32BaseSystem::totalHeap(), totalHeap, sizeof(totalHeap));
    formatReadableBytes(Esp32BaseSystem::flashSize(), flash, sizeof(flash));
    uint32_t imageSize = 0;
    const bool hasImageSize = runningImageSize(imageSize);
    if (hasImageSize) {
        formatReadableBytes(imageSize, imageSizeText, sizeof(imageSizeText));
    }
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
#if ESP32BASE_ENABLE_WIFI
    const bool wifiConnected = Esp32BaseWiFi::isConnected();
    const bool wifiWarn = Esp32BaseWiFi::safeBootPaused() || Esp32BaseWiFi::state() == Esp32BaseWiFi::FAILED;
    char wifiIp[24] = "-";
    if (wifiConnected) {
        Esp32BaseWiFi::ip(wifiIp, sizeof(wifiIp));
    }
#endif

    sendChunk("<section class='panel statusidentity'><div class='metrics'>");
    sendChunk("<div><b>Firmware</b><span>");
    sendEscapedHtmlChunk(Esp32Base::firmwareName());
    sendChunk(" ");
    sendEscapedHtmlChunk(Esp32Base::firmwareVersion());
    sendChunk("</span></div><div><b>Hostname</b><span>");
    sendEscapedHtmlChunk(Esp32Base::hostname());
    sendChunk("</span></div><div><b>Profile</b><span>");
    sendEscapedHtmlChunk(Esp32Base::profileName());
    sendChunk("</span></div><div><b>Uptime</b><span>");
    sendEscapedHtmlChunk(uptime);
    sendChunk("</span></div></div><p class='statusmeta'>Boot count ");
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(Esp32BaseSystem::bootCount()));
    sendEscapedHtmlChunk(value);
    if (Esp32Base::firmwareBuild()[0]) {
        sendChunk(" · Build ");
        sendEscapedHtmlChunk(Esp32Base::firmwareBuild());
    }
    sendChunk("</p>");
#if ESP32BASE_ENABLE_WIFI
    sendChunk("<div class='statusnetworkquick'><div class='statusnetworkcopy'><b>Network setup</b><span>");
    if (wifiConnected && Esp32BaseWiFi::ssid()[0]) {
        sendEscapedHtmlChunk(Esp32BaseWiFi::ssid());
        sendChunk(" · ");
        sendEscapedHtmlChunk(wifiIp);
    } else if (Esp32BaseWiFi::state() == Esp32BaseWiFi::CONFIG_PORTAL) {
        sendChunk("Configuration hotspot is active");
    } else {
        sendChunk("Choose or update the device WiFi network");
    }
    sendChunk("</span></div><a class='btnlink info statuswifibutton' href='/esp32base/wifi'>Configure WiFi</a></div>");
#endif
    sendChunk("</section>");

    const bool configStalled = Esp32BaseConfig::isDeferredFlushPaused() && Esp32BaseConfig::pendingCount() > 0;
    bool hasAttention = resetReasonNeedsAttention() || configStalled || Esp32Base::lastError()[0];
#if ESP32BASE_ENABLE_WIFI
    hasAttention = hasAttention || Esp32BaseWiFi::safeBootPaused() || Esp32BaseWiFi::state() == Esp32BaseWiFi::FAILED;
#endif
#if ESP32BASE_ENABLE_FS
    hasAttention = hasAttention || !Esp32BaseFs::isReady();
#endif
#if ESP32BASE_ENABLE_FILELOG
    hasAttention = hasAttention || Esp32BaseFileLog::faulted();
#endif
#if ESP32BASE_ENABLE_OTA
    hasAttention = hasAttention || Esp32BaseOta::status() == Esp32BaseOta::FAILED || Esp32BaseOta::waitingForMarkValid();
#endif
    if (hasAttention) {
        sendChunk("<section class='panel statusattention'><h2>Attention</h2><ul class='statusissues'>");
        if (resetReasonNeedsAttention()) {
            sendChunk("<li>Previous reset: "); sendEscapedHtmlChunk(Esp32BaseSystem::resetReason()); sendChunk("</li>");
        }
        if (Esp32Base::lastError()[0]) {
            sendChunk("<li>Startup error: "); sendEscapedHtmlChunk(Esp32Base::lastError()); sendChunk("</li>");
        }
        if (configStalled) sendChunk("<li>Deferred configuration writes are paused.</li>");
#if ESP32BASE_ENABLE_WIFI
        if (Esp32BaseWiFi::safeBootPaused()) sendChunk("<li>Saved WiFi recovery is paused by the guarded-reset policy.</li>");
        if (Esp32BaseWiFi::state() == Esp32BaseWiFi::FAILED) sendChunk("<li>WiFi connection attempts have failed.</li>");
#endif
#if ESP32BASE_ENABLE_FS
        if (!Esp32BaseFs::isReady()) sendChunk("<li>File system is unavailable.</li>");
#endif
#if ESP32BASE_ENABLE_FILELOG
        if (Esp32BaseFileLog::faulted()) sendChunk("<li>System file logging is faulted.</li>");
#endif
#if ESP32BASE_ENABLE_OTA
        if (Esp32BaseOta::status() == Esp32BaseOta::FAILED) sendChunk("<li>The last OTA operation failed.</li>");
        if (Esp32BaseOta::waitingForMarkValid()) sendChunk("<li>The running OTA image is waiting to be marked valid.</li>");
#endif
        sendChunk("</ul></section>");
    }

    sendChunk("<div class='statusgrid'>");

#if ESP32BASE_ENABLE_WIFI
    sendStatusCardStart("Network", wifiWarn ? Esp32BaseWeb::UI_WARN : (wifiConnected ? Esp32BaseWeb::UI_OK : Esp32BaseWeb::UI_NEUTRAL), Esp32BaseWiFi::stateName());
    sendInfoRowStart("WiFi");
    if (Esp32BaseWiFi::ssid()[0]) {
        sendEscapedHtmlChunk(Esp32BaseWiFi::ssid());
    } else {
        sendChunk("No saved network");
    }
    sendInfoRowEnd();
    sendInfoRow("IP", wifiIp);
    if (wifiConnected) {
        snprintf(value, sizeof(value), "%ld dBm", static_cast<long>(Esp32BaseWiFi::rssi()));
    } else {
        strlcpy(value, "-", sizeof(value));
    }
    sendInfoRow("RSSI", value);
    sendInfoRowStart("Retry");
    sendSubmetricsStart();
    snprintf(value, sizeof(value), "%u", static_cast<unsigned>(Esp32BaseWiFi::retryCount()));
    sendSubmetric("Attempts", value);
    snprintf(value, sizeof(value), "%lu ms", static_cast<unsigned long>(Esp32BaseWiFi::retryRemainingMs()));
    sendSubmetric("Next", value);
    sendSubmetricsEnd();
    sendInfoRowEnd();
    sendTaggedInfoRow("Recovery policy", Esp32BaseWiFi::safeBootPaused() ? "paused" : "available", Esp32BaseWiFi::safeBootPaused() ? Esp32BaseWeb::UI_WARN : Esp32BaseWeb::UI_OK);
    sendInfoRow("Power save", Esp32BaseWiFi::powerSave() ? "on" : "off");
#if ESP32BASE_ENABLE_MDNS
    sendTaggedInfoRow("mDNS", Esp32BaseMdns::isRunning() ? "running" : "stopped", Esp32BaseMdns::isRunning() ? Esp32BaseWeb::UI_OK : Esp32BaseWeb::UI_NEUTRAL);
#endif
    char address[24];
    formatIpAddress(WiFi.gatewayIP(), address, sizeof(address)); sendInfoRow("Gateway", address);
    formatIpAddress(WiFi.subnetMask(), address, sizeof(address)); sendInfoRow("Subnet", address);
    formatIpAddress(WiFi.dnsIP(), address, sizeof(address)); sendInfoRow("DNS", address);
    snprintf(value, sizeof(value), "%d", WiFi.channel()); sendInfoRow("Channel", value);
    sendInfoRow("STA MAC", WiFi.macAddress().c_str());
    sendInfoRow("AP MAC", WiFi.softAPmacAddress().c_str());
    sendStatusSectionEnd();
#endif

    sendStatusCardStart("Runtime",
                        resetReasonNeedsAttention() ? Esp32BaseWeb::UI_WARN : Esp32BaseWeb::UI_OK,
                        resetReasonNeedsAttention() ? "check reset" : "healthy",
                        "statusruntime");
    sendInfoRowStart("Heap");
    sendSubmetricsStart();
    sendSubmetric("Free", freeHeap);
    sendSubmetric("Min", minHeap);
    sendSubmetric("Max alloc", maxAllocHeap);
    sendSubmetric("Total", totalHeap);
    sendSubmetricsEnd();
    sendInfoRowEnd();
    const uint32_t stackFreeBytes = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)) * sizeof(StackType_t);
    formatReadableBytes(stackFreeBytes, value, sizeof(value));
    sendInfoRow("Loop stack low-water", value);
#if ESP32BASE_ENABLE_HEALTH
    sendInfoRowStart("Loop health");
    sendSubmetricsStart();
    snprintf(value, sizeof(value), "%lu ms", static_cast<unsigned long>(Esp32BaseHealth::loopPeriodMaxMs()));
    sendSubmetric("Max period", value);
    const uint32_t healthAge = millis() - Esp32BaseHealth::lastTickMs();
    snprintf(value, sizeof(value), "%lu ms", static_cast<unsigned long>(healthAge));
    sendSubmetric("Tick age", value);
    sendSubmetricsEnd();
    sendInfoRowEnd();
#endif
    if (ESP.getPsramSize() > 0) {
        char psramTotal[48], psramFree[48], psramMin[48], psramMax[48];
        formatReadableBytes(ESP.getPsramSize(), psramTotal, sizeof(psramTotal));
        formatReadableBytes(ESP.getFreePsram(), psramFree, sizeof(psramFree));
        formatReadableBytes(ESP.getMinFreePsram(), psramMin, sizeof(psramMin));
        formatReadableBytes(ESP.getMaxAllocPsram(), psramMax, sizeof(psramMax));
        sendInfoRowStart("PSRAM"); sendSubmetricsStart();
        sendSubmetric("Free", psramFree); sendSubmetric("Min", psramMin); sendSubmetric("Max alloc", psramMax); sendSubmetric("Total", psramTotal);
        sendSubmetricsEnd(); sendInfoRowEnd();
    }
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
#if ESP32BASE_ENABLE_TIME
    const Esp32BaseTime::Snapshot baseTime = Esp32BaseTime::snapshot();
    sendInfoRowStart("Time");
    if (baseTime.synced) {
        sendStatusTag(Esp32BaseWeb::UI_OK, "synced");
    } else {
        sendStatusTag(Esp32BaseWeb::UI_NEUTRAL, "uptime only");
    }
    sendSubmetricsStart();
    sendSubmetric("Source", Esp32BaseTime::sourceName(baseTime.source));
    if (baseTime.synced && Esp32BaseTime::formatTime(value, sizeof(value), "%Y-%m-%d %H:%M:%S")) {
        sendSubmetric("Current", value);
    }
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(baseTime.uptimeSec));
    sendSubmetric("Uptime sec", value);
    sendSubmetricsEnd();
    sendInfoRowEnd();
#endif
#if ESP32BASE_ENABLE_RTC
    sendInfoRowStart("RTC");
    if (Esp32BaseRtc::status() == Esp32BaseRtc::STATUS_OK) {
        sendStatusTag(Esp32BaseWeb::UI_OK, Esp32BaseRtc::statusText());
    } else if (Esp32BaseRtc::status() == Esp32BaseRtc::STATUS_NOT_STARTED) {
        sendStatusTag(Esp32BaseWeb::UI_NEUTRAL, Esp32BaseRtc::statusText());
    } else {
        sendStatusTag(Esp32BaseWeb::UI_WARN, Esp32BaseRtc::statusText());
    }
    sendSubmetricsStart();
    sendSubmetric("Driver", Esp32BaseRtc::driverName());
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(Esp32BaseRtc::lastEpoch()));
    sendSubmetric("Last epoch", value);
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(Esp32BaseRtc::lastSyncUptimeSec()));
    sendSubmetric("Last sync uptime", value);
    sendSubmetricsEnd();
    sendInfoRowEnd();
#endif
#if ESP32BASE_ENABLE_NTP
    const Esp32BaseNtp::TimeSnapshot ntpTime = Esp32BaseNtp::snapshot();
    sendInfoRowStart("NTP");
    if (!Esp32BaseNtp::isStarted()) {
        sendStatusTag(Esp32BaseWeb::UI_NEUTRAL, "not started");
    } else if (ntpTime.synced) {
        sendStatusTag(Esp32BaseWeb::UI_OK, "synced");
    } else {
        sendStatusTag(Esp32BaseWeb::UI_WARN, "pending");
    }
    if (ntpTime.synced && Esp32BaseNtp::formatTime(value, sizeof(value), "%Y-%m-%d %H:%M:%S")) {
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

    const uint8_t pendingConfig = Esp32BaseConfig::pendingCount();
    sendStatusCardStart("Persistence", configStalled ? Esp32BaseWeb::UI_WARN : Esp32BaseWeb::UI_OK, configStalled ? "writes paused" : "ready");
    sendInfoRowStart("Configuration");
    sendSubmetricsStart();
    snprintf(value, sizeof(value), "%u / %u", static_cast<unsigned>(pendingConfig), static_cast<unsigned>(Esp32BaseConfig::pendingCapacity()));
    sendSubmetric("Pending", value);
    sendSubmetric("Flush", Esp32BaseConfig::isDeferredFlushPaused() ? "paused" : "running");
    sendSubmetricsEnd();
    sendInfoRowEnd();
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
    if (Esp32BaseFileLog::bufferEnabled()) {
        snprintf(value, sizeof(value), "%u / %u bytes", static_cast<unsigned>(Esp32BaseFileLog::bufferUsed()), static_cast<unsigned>(Esp32BaseFileLog::bufferSize()));
        sendInfoRow("Log buffer", value);
    }
#endif
#if ESP32BASE_ENABLE_APP_EVENTS
    Esp32BaseAppEvents::AppEventsStatus appEventStatus = {};
    if (Esp32BaseAppEvents::readStatus(appEventStatus)) {
        sendInfoRowStart("Application events");
        sendStatusTag(appEventStatus.eventStore.ready ? Esp32BaseWeb::UI_OK : Esp32BaseWeb::UI_WARN,
                      Esp32BaseRecordStore::storeStateName(appEventStatus.eventStore.state));
        sendSubmetricsStart();
        snprintf(value, sizeof(value), "%lu / %lu", static_cast<unsigned long>(appEventStatus.eventStore.recordCount), static_cast<unsigned long>(appEventStatus.eventStore.capacity));
        sendSubmetric("Records", value);
        snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(appEventStatus.eventStore.damagedRecordCount));
        sendSubmetric("Damaged", value);
        sendSubmetricsEnd(); sendInfoRowEnd();
    }
#endif
#if ESP32BASE_ENABLE_RECORD_STORE
    uint32_t businessRecords = 0;
    uint8_t businessFaults = 0;
    const uint8_t storeCount = businessRecordStoreCount();
    for (uint8_t i = 0; i < storeCount; ++i) {
        Esp32BaseRecordStore* store = businessRecordStoreAt(i);
        Esp32BaseRecordStore::StoreStatus storeStatus = {};
        if (!store || !store->readStatus(storeStatus) || !storeStatus.ready || storeStatus.damagedRecordCount > 0) ++businessFaults;
        businessRecords += storeStatus.recordCount;
    }
    if (storeCount > 0) {
        sendInfoRowStart("Business stores");
        sendStatusTag(businessFaults ? Esp32BaseWeb::UI_WARN : Esp32BaseWeb::UI_OK, businessFaults ? "check" : "ready");
        sendSubmetricsStart();
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(storeCount)); sendSubmetric("Stores", value);
        snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(businessRecords)); sendSubmetric("Records", value);
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(businessFaults)); sendSubmetric("Faults", value);
        sendSubmetricsEnd(); sendInfoRowEnd();
    }
#endif
    nvs_stats_t nvsStats = {};
    if (nvs_get_stats(nullptr, &nvsStats) == ESP_OK) {
        sendInfoRowStart("NVS entries"); sendSubmetricsStart();
        snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(nvsStats.used_entries)); sendSubmetric("Used", value);
        snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(nvsStats.free_entries)); sendSubmetric("Free", value);
        snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(nvsStats.total_entries)); sendSubmetric("Total", value);
        snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(nvsStats.namespace_count)); sendSubmetric("Namespaces", value);
        sendSubmetricsEnd(); sendInfoRowEnd();
    }
    sendStatusSectionEnd();

    bool imageFitsTarget = hasImageSize && nextPartition && imageSize <= nextPartition->size;
    Esp32BaseWeb::UiTone firmwareTone = (!hasImageSize || (nextPartition && !imageFitsTarget)) ? Esp32BaseWeb::UI_WARN : Esp32BaseWeb::UI_OK;
    sendStatusCardStart(
#if ESP32BASE_ENABLE_OTA
        "Firmware & OTA",
#else
        "Firmware",
#endif
        firmwareTone, hasImageSize ? "measured" : "size unavailable");
    sendInfoRow("Current image", imageSizeText);
    sendInfoRow("Running slot", runningSlot);
    sendInfoRow("Boot slot", bootSlot);
    sendInfoRow("OTA target slot", otaTargetSlot);
    sendInfoRow("Max OTA upload", otaTargetSize);
    sendInfoRow("Fits OTA target", hasImageSize && nextPartition ? (imageFitsTarget ? "yes" : "no") : "unknown");
#if ESP32BASE_ENABLE_OTA
    sendInfoRow("OTA service", otaStatusName(Esp32BaseOta::status()));
    sendInfoRow("Running image state", Esp32BaseOta::runningOtaState());
    if (Esp32BaseOta::waitingForMarkValid()) {
        sendInfoRowStart("Mark-valid window"); sendSubmetricsStart();
        snprintf(value, sizeof(value), "%lu ms", static_cast<unsigned long>(Esp32BaseOta::markValidElapsedMs())); sendSubmetric("Elapsed", value);
        snprintf(value, sizeof(value), "%lu ms", static_cast<unsigned long>(Esp32BaseOta::markValidTimeoutMs())); sendSubmetric("Timeout", value);
        sendSubmetricsEnd(); sendInfoRowEnd();
    }
    sendInfoRow("Rollback", Esp32BaseOta::isRollbackPossible() ? "possible" : "not possible");
    if (Esp32BaseOta::lastError()[0]) {
        sendInfoRow("Last OTA error", Esp32BaseOta::lastError());
    }
#endif
    sendFirmwareOtaDetails();
    sendStatusSectionEnd();

    char mac[18];
    sendStatusCardStart("Platform & Security", Esp32BaseWeb::isAuthEnabled() ? Esp32BaseWeb::UI_OK : Esp32BaseWeb::UI_WARN, Esp32BaseWeb::isAuthEnabled() ? "protected" : "auth off");
    sendInfoRow("Chip", ESP.getChipModel());
    snprintf(value, sizeof(value), "%d", ESP.getChipRevision());
    sendInfoRow("Revision", value);
    snprintf(value, sizeof(value), "%u", static_cast<unsigned>(ESP.getChipCores()));
    sendInfoRow("Cores", value);
    snprintf(value, sizeof(value), "%u MHz", static_cast<unsigned>(ESP.getCpuFreqMHz()));
    sendInfoRow("CPU", value);
    sendInfoRow("SDK", ESP.getSdkVersion());
    sendInfoRow("Flash chip", flash);
    sendInfoRow("Flash encryption", esp_flash_encryption_enabled() ? "enabled" : "disabled");
    sendInfoRow("Secure boot", esp_secure_boot_enabled() ? "enabled" : "disabled");
    sendInfoRow("Web authentication", Esp32BaseWeb::isAuthEnabled() ? "enabled" : "disabled");
    formatMac(ESP.getEfuseMac(), mac, sizeof(mac));
    sendInfoRow("eFuse MAC", mac);
    sendStatusSectionEnd();
    sendChunk("</div>");

    if (g_homeMode == Esp32BaseWeb::HOME_ESP32BASE && appNavCount() > 0) {
        sendChunk("<section class='panel appsection'><h2>Application</h2>");
        sendAppLinks(true, nullptr);
        sendChunk("</section>");
    }

    Esp32BaseWeb::sendFooter();
}

} // namespace esp32base_web

#endif
