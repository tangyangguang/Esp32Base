#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB && ESP32BASE_ENABLE_OTA

#include "WebInternal.h"
#include "WebOtaPreflight.h"

#include <stdlib.h>
#include <string.h>

namespace esp32base_web {

#if ESP32BASE_ENABLE_OTA
namespace {
constexpr size_t kRawOtaWriteBufferSize = 64UL * 1024UL;
constexpr size_t kRawOtaFlashWriteChunkSize = 4UL * 1024UL;
uint8_t* g_rawOtaWriteBuffer = nullptr;
size_t g_rawOtaWriteBuffered = 0;

void resetRawOtaWriteBuffer() {
    if (g_rawOtaWriteBuffer) {
        free(g_rawOtaWriteBuffer);
        g_rawOtaWriteBuffer = nullptr;
    }
    g_rawOtaWriteBuffered = 0;
}

bool ensureRawOtaWriteBuffer() {
    if (g_rawOtaWriteBuffer) {
        return true;
    }
    g_rawOtaWriteBuffer = static_cast<uint8_t*>(malloc(kRawOtaWriteBufferSize));
    return g_rawOtaWriteBuffer != nullptr;
}

bool flushRawOtaWriteBuffer() {
    if (g_rawOtaWriteBuffered == 0) {
        return true;
    }
    const size_t len = g_rawOtaWriteBuffered;
    g_rawOtaWriteBuffered = 0;
    size_t offset = 0;
    while (offset < len) {
        const size_t remaining = len - offset;
        const size_t writeLen = remaining < kRawOtaFlashWriteChunkSize ? remaining : kRawOtaFlashWriteChunkSize;
        if (!Esp32BaseOta::writeChunk(g_rawOtaWriteBuffer + offset, writeLen)) {
            return false;
        }
        offset += writeLen;
    }
    return true;
}

bool appendRawOtaWriteBuffer(const uint8_t* data, size_t len) {
    if (!ensureRawOtaWriteBuffer()) {
        Esp32BaseOta::abortUpload("raw ota buffer allocation failed");
        return false;
    }
    size_t offset = 0;
    while (offset < len) {
        if (g_rawOtaWriteBuffered == kRawOtaWriteBufferSize && !flushRawOtaWriteBuffer()) {
            return false;
        }
        const size_t available = kRawOtaWriteBufferSize - g_rawOtaWriteBuffered;
        const size_t copyLen = (len - offset) < available ? (len - offset) : available;
        memcpy(g_rawOtaWriteBuffer + g_rawOtaWriteBuffered, data + offset, copyLen);
        g_rawOtaWriteBuffered += copyLen;
        offset += copyLen;
    }
    return true;
}
} // namespace

bool parseSizeHeader(const String& value, size_t& out, char* error, size_t errorLen) {
    return parseOtaDeclaredSize(value.c_str(), out, error, errorLen);
}

bool validateDeclaredOtaSizeForCurrentTarget(size_t firmwareSize, char* error, size_t errorLen) {
    const esp_partition_t* nextPartition = esp_ota_get_next_update_partition(nullptr);
    return validateOtaDeclaredSize(firmwareSize, nextPartition ? nextPartition->size : 0, error, errorLen);
}

void serviceOtaUploadRequest() {
#if ESP32BASE_ENABLE_WATCHDOG
    Esp32BaseWatchdog::feed();
#endif
    yield();
}

void serviceRejectedOtaUpload() {
    serviceOtaUploadRequest();
}

void formatOtaAbortReason(const char* endpoint, const char* reason, char* out, size_t outLen) {
    if (!out || outLen == 0) {
        return;
    }
    snprintf(out,
             outLen,
             "%s endpoint=%s bytes=%lu total=%lu progress=%u%%",
             reason && reason[0] ? reason : "client aborted ota upload",
             endpoint && endpoint[0] ? endpoint : "unknown",
             static_cast<unsigned long>(Esp32BaseOta::bytesProcessed()),
             static_cast<unsigned long>(Esp32BaseOta::totalSize()),
             static_cast<unsigned>(Esp32BaseOta::progress()));
}

void formatRawOtaAbortReason(const HTTPRaw& raw, char* out, size_t outLen) {
    if (!out || outLen == 0) {
        return;
    }
    snprintf(out,
             outLen,
             "client aborted raw upload endpoint=/esp32base/ota/raw written=%lu buffered=%lu raw=%lu/%d progress=%u%%",
             static_cast<unsigned long>(Esp32BaseOta::bytesProcessed()),
             static_cast<unsigned long>(g_rawOtaWriteBuffered),
             static_cast<unsigned long>(raw.totalSize),
             g_server.clientContentLength(),
             static_cast<unsigned>(Esp32BaseOta::progress()));
}

void handleOtaPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    Esp32BaseWeb::sendHeader(g_builtinLabels[Esp32BaseWeb::BUILTIN_OTA]);
    Esp32BaseWeb::sendPageTitle("OTA Update", "Upload a firmware binary through the authenticated Web OTA endpoint.");
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    char runningText[80] = "unknown";
    char bootText[80] = "unknown";
    char nextText[80] = "unavailable";
    size_t nextSize = 0;
    if (running) {
        snprintf(runningText, sizeof(runningText), "%s / 0x%06lx", running->label, static_cast<unsigned long>(running->address));
    }
    if (boot) {
        snprintf(bootText, sizeof(bootText), "%s / 0x%06lx", boot->label, static_cast<unsigned long>(boot->address));
    }
    if (next) {
        nextSize = next->size;
        snprintf(nextText, sizeof(nextText), "%s / 0x%06lx", next->label, static_cast<unsigned long>(next->address));
    }
    sendChunk("<section class='panel statuspage'><h2>OTA diagnostics</h2><div class='tablewrap'><table class='kv'>");
    sendInfoRow("Running slot", runningText);
    sendInfoRow("Boot slot", bootText);
    sendInfoRow("Next update slot", nextText);
    sendInfoRow("Running ELF SHA256", []() -> const char* {
        static char sha[65];
        sha[0] = '\0';
        esp_ota_get_app_elf_sha256(sha, sizeof(sha));
        return sha[0] ? sha : "unavailable";
    }());
    sendChunk("</table></div></section>");
    char formStart[160];
    snprintf(formStart, sizeof(formStart), "<section class='panel formpanel uploadpanel'><h2>Firmware upload</h2><form id='f' class='editform' data-max-size='%lu'>", static_cast<unsigned long>(nextSize));
    sendChunk(formStart);
    sendChunk("<div class='fieldgrid'><div class='field full'><label for='fw'>Firmware file</label><input id='fw' type='file' name='firmware' accept='.bin' required></div><div class='field long'><label for='sha'>SHA256</label><input id='sha' placeholder='Optional 64-character digest' maxlength='64'><small>Optional integrity check for the uploaded firmware.</small></div></div><div class='actions'><input type='submit' value='Upload Firmware'></div></form><progress id='p' value='0' max='100' style='width:100%;display:none'></progress><p id='s' class='statusline muted'></p></section>");
    sendChunk("<script>function h(n){var u=['B','KB','MB','GB'],i=0,x=n;while(x>=1024&&i<u.length-1){x/=1024;i++;}return (i?x.toFixed(2):Math.round(x))+' '+u[i];}var form=document.getElementById('f'),fw=document.getElementById('fw'),st=document.getElementById('s');function maxSize(){return parseInt(form.dataset.maxSize||'0',10)||0;}function showFile(){var f=fw.files[0],max=maxSize();if(!f){st.textContent='';return;}st.textContent='Selected firmware '+h(f.size)+(max>0?' / OTA slot '+h(max):'');}fw.onchange=showFile;form.onsubmit=function(e){e.preventDefault();if(this.dataset.busy)return false;this.dataset.busy=1;var f=fw.files[0],pg=document.getElementById('p'),b=this.querySelector('[type=submit]'),max=maxSize();if(!f){this.dataset.busy='';return false;}if(max>0&&f.size>max){st.textContent='Upload blocked: firmware '+h(f.size)+' exceeds OTA slot '+h(max);this.dataset.busy='';return false;}var d=new FormData();d.append('firmware',f,f.name);var x=new XMLHttpRequest();if(b)b.disabled=true;pg.style.display='block';st.textContent='Uploading 0%';x.upload.onprogress=function(ev){if(ev.lengthComputable){var p=Math.floor(ev.loaded*100/ev.total);pg.value=p;st.textContent='Uploading '+p+'% '+h(ev.loaded)+' / '+h(ev.total);}};x.onload=function(){var r={};try{r=JSON.parse(x.responseText||'{}');}catch(e){}if(x.status==200){st.textContent='Upload successful. Restarting...';setTimeout(function(){location.href=r.redirect||'/esp32base';},8000);return;}st.textContent='Upload failed: '+(r.error||('HTTP '+x.status));document.getElementById('f').dataset.busy='';if(b)b.disabled=false;};x.onerror=function(){st.textContent='Upload failed: network error';document.getElementById('f').dataset.busy='';if(b)b.disabled=false;};x.open('POST','/esp32base/ota');x.setRequestHeader('X-Firmware-Size',String(f.size));var s=document.getElementById('sha').value;if(s)x.setRequestHeader('X-Sha256',s);x.send(d);return false;};</script>");
    Esp32BaseWeb::sendFooter();
}

void handleOtaUploadDone() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    if (g_otaUploadForbidden || !requestSameOrigin()) {
        ESP32BASE_LOG_W("web", "post_rejected context=ota_upload reason=cross_origin");
        g_otaUploadForbidden = false;
        g_otaUploadStartFailed = false;
        g_server.send(403, "application/json", "{\"ok\":false,\"error\":\"forbidden\"}");
        return;
    }
    g_otaUploadForbidden = false;
    const bool startFailed = g_otaUploadStartFailed;
    g_otaUploadStartFailed = false;
    const bool ok = Esp32BaseOta::status() == Esp32BaseOta::SUCCESS;
    if (ok) {
        if (beginResponse(200, "application/json", nullptr)) {
            sendChunk("{\"ok\":true,\"redirect\":\"");
            sendEscapedJsonChunk(configuredHomePath());
            sendChunk("\",\"rssi\":");
            sendIntChunk(Esp32BaseWiFi::isConnected() ? Esp32BaseWiFi::rssi() : 0);
            sendChunk(",\"elapsedMs\":");
            sendIntChunk(Esp32BaseOta::elapsedMs());
            sendChunk(",\"averageBytesPerSecond\":");
            sendIntChunk(Esp32BaseOta::averageBytesPerSecond());
            sendChunk(",\"calculatedSha256\":\"");
            sendEscapedJsonChunk(Esp32BaseOta::calculatedSha256());
            sendChunk("\",\"bootPartition\":\"");
            sendEscapedJsonChunk(Esp32BaseOta::lastBootPartitionLabel());
            sendChunk("\",\"targetPartition\":\"");
            sendEscapedJsonChunk(Esp32BaseOta::lastTargetPartitionLabel());
            sendChunk("\"");
            sendChunk("}");
            endResponse();
        }
    } else {
        if (beginResponse(500, "application/json", nullptr)) {
            sendChunk("{\"ok\":false,\"error\":\"");
            sendEscapedJsonChunk(startFailed && !Esp32BaseOta::lastError()[0] ? "ota upload rejected" : Esp32BaseOta::lastError());
            sendChunk("\"}");
            endResponse();
        }
    }
    if (ok) {
        delay(500);
        Esp32BaseSystem::restart("ota success");
    }
}

void handleOtaUpload() {
    if (!isAuthenticated()) {
        if (g_server.upload().status == UPLOAD_FILE_START) {
            Esp32BaseOta::rejectUpload("unauthorized ota upload");
        }
        return;
    }
    HTTPUpload& upload = g_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        g_otaUploadForbidden = false;
        g_otaUploadStartFailed = false;
        if (!requestSameOrigin()) {
            g_otaUploadForbidden = true;
            ESP32BASE_LOG_W("web", "post_rejected context=ota_upload reason=cross_origin");
            Esp32BaseOta::rejectUpload("forbidden ota upload origin");
            return;
        }
        char sha256[65] = "";
        size_t firmwareSize = 0;
        if (g_server.hasHeader("X-Sha256")) {
            strlcpy(sha256, g_server.header("X-Sha256").c_str(), sizeof(sha256));
        } else if (g_server.hasArg("sha256")) {
            strlcpy(sha256, g_server.arg("sha256").c_str(), sizeof(sha256));
        }
        char sizeError[96];
        if (!parseSizeHeader(g_server.hasHeader("X-Firmware-Size") ? g_server.header("X-Firmware-Size") : String(""), firmwareSize, sizeError, sizeof(sizeError)) ||
            !validateDeclaredOtaSizeForCurrentTarget(firmwareSize, sizeError, sizeof(sizeError))) {
            g_otaUploadStartFailed = true;
            Esp32BaseOta::rejectUpload(sizeError);
            ESP32BASE_LOG_W("web", "ota_upload_start_rejected error=%s", Esp32BaseOta::lastError());
            return;
        }
        if (!Esp32BaseOta::startUpload(firmwareSize, sha256[0] ? sha256 : nullptr)) {
            g_otaUploadStartFailed = true;
            ESP32BASE_LOG_W("web", "ota_upload_start_rejected error=%s", Esp32BaseOta::lastError());
            return;
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (g_otaUploadForbidden || g_otaUploadStartFailed) {
            serviceRejectedOtaUpload();
            return;
        }
        Esp32BaseOta::writeChunk(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
        if (g_otaUploadForbidden || g_otaUploadStartFailed) {
            return;
        }
        Esp32BaseOta::finishUpload();
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        char reason[96];
        formatOtaAbortReason("/esp32base/ota", "client aborted multipart upload", reason, sizeof(reason));
        Esp32BaseOta::abortUpload(reason);
    }
}

void handleOtaRawUpload() {
    if (!isAuthenticated()) {
        if (g_server.raw().status == RAW_START) {
            Esp32BaseOta::rejectUpload("unauthorized ota raw upload");
        }
        return;
    }
    HTTPRaw& raw = g_server.raw();
    if (raw.status == RAW_START) {
        resetRawOtaWriteBuffer();
        g_otaUploadForbidden = false;
        g_otaUploadStartFailed = false;
        if (!requestSameOrigin()) {
            g_otaUploadForbidden = true;
            ESP32BASE_LOG_W("web", "post_rejected context=ota_raw_upload reason=cross_origin");
            Esp32BaseOta::rejectUpload("forbidden ota raw upload origin");
            return;
        }
        char sha256[65] = "";
        size_t firmwareSize = 0;
        if (g_server.hasHeader("X-Sha256")) {
            strlcpy(sha256, g_server.header("X-Sha256").c_str(), sizeof(sha256));
        }
        char sizeError[96];
        if (!parseSizeHeader(g_server.hasHeader("X-Firmware-Size") ? g_server.header("X-Firmware-Size") : String(""), firmwareSize, sizeError, sizeof(sizeError)) ||
            !validateDeclaredOtaSizeForCurrentTarget(firmwareSize, sizeError, sizeof(sizeError))) {
            g_otaUploadStartFailed = true;
            Esp32BaseOta::rejectUpload(sizeError);
            ESP32BASE_LOG_W("web", "ota_raw_upload_start_rejected error=%s", Esp32BaseOta::lastError());
            return;
        }
        if (!Esp32BaseOta::startUpload(firmwareSize, sha256[0] ? sha256 : nullptr)) {
            g_otaUploadStartFailed = true;
            ESP32BASE_LOG_W("web", "ota_raw_upload_start_rejected error=%s", Esp32BaseOta::lastError());
            return;
        }
        serviceOtaUploadRequest();
    } else if (raw.status == RAW_WRITE) {
        if (g_otaUploadForbidden || g_otaUploadStartFailed) {
            serviceRejectedOtaUpload();
            return;
        }
        const size_t total = Esp32BaseOta::totalSize();
        const size_t accepted = Esp32BaseOta::bytesProcessed() + g_rawOtaWriteBuffered;
        if (accepted >= total) {
            serviceOtaUploadRequest();
            return;
        }
        const size_t remaining = total - accepted;
        const size_t writeLen = raw.currentSize > remaining ? remaining : raw.currentSize;
        if (writeLen > 0 && !appendRawOtaWriteBuffer(raw.buf, writeLen)) {
            g_otaUploadStartFailed = true;
            resetRawOtaWriteBuffer();
        }
        serviceOtaUploadRequest();
    } else if (raw.status == RAW_END) {
        if (g_otaUploadForbidden || g_otaUploadStartFailed) {
            resetRawOtaWriteBuffer();
            return;
        }
        if (!flushRawOtaWriteBuffer()) {
            resetRawOtaWriteBuffer();
            return;
        }
        resetRawOtaWriteBuffer();
        Esp32BaseOta::finishUpload();
    } else if (raw.status == RAW_ABORTED) {
        char reason[160];
        formatRawOtaAbortReason(raw, reason, sizeof(reason));
        ESP32BASE_LOG_W("web",
                        "ota_raw_aborted written=%lu buffered=%lu raw_total=%lu raw_current=%lu content_length=%d",
                        static_cast<unsigned long>(Esp32BaseOta::bytesProcessed()),
                        static_cast<unsigned long>(g_rawOtaWriteBuffered),
                        static_cast<unsigned long>(raw.totalSize),
                        static_cast<unsigned long>(raw.currentSize),
                        g_server.clientContentLength());
        resetRawOtaWriteBuffer();
        Esp32BaseOta::abortUpload(reason);
    }
}

void handleOtaApi() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    if (!beginResponse(200, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"status\":");
    sendIntChunk(static_cast<int>(Esp32BaseOta::status()));
    sendChunk(",\"progress\":");
    sendIntChunk(Esp32BaseOta::progress());
    sendChunk(",\"bytesProcessed\":");
    sendBytesJsonChunk(static_cast<uint32_t>(Esp32BaseOta::bytesProcessed()));
    sendChunk(",\"totalSize\":");
    sendBytesJsonChunk(static_cast<uint32_t>(Esp32BaseOta::totalSize()));
    sendChunk(",\"elapsedMs\":");
    sendIntChunk(Esp32BaseOta::elapsedMs());
    sendChunk(",\"averageBytesPerSecond\":");
    sendIntChunk(Esp32BaseOta::averageBytesPerSecond());
    sendChunk(",\"error\":\"");
    sendEscapedJsonChunk(Esp32BaseOta::lastError());
    sendChunk("\",\"expectedSha256\":\"");
    sendEscapedJsonChunk(Esp32BaseOta::expectedSha256());
    sendChunk("\",\"calculatedSha256\":\"");
    sendEscapedJsonChunk(Esp32BaseOta::calculatedSha256());
    sendChunk("\",\"lastTargetPartition\":{\"label\":\"");
    sendEscapedJsonChunk(Esp32BaseOta::lastTargetPartitionLabel());
    sendChunk("\",\"address\":");
    sendIntChunk(static_cast<int>(Esp32BaseOta::lastTargetPartitionAddress()));
    sendChunk(",\"size\":");
    sendBytesJsonChunk(static_cast<uint32_t>(Esp32BaseOta::lastTargetPartitionSize()));
    sendChunk("},\"lastBootPartition\":{\"label\":\"");
    sendEscapedJsonChunk(Esp32BaseOta::lastBootPartitionLabel());
    sendChunk("\",\"address\":");
    sendIntChunk(static_cast<int>(Esp32BaseOta::lastBootPartitionAddress()));
    sendChunk(",\"size\":");
    sendBytesJsonChunk(static_cast<uint32_t>(Esp32BaseOta::lastBootPartitionSize()));
    sendChunk("},");
    sendPartitionJson("runningPartition", esp_ota_get_running_partition());
    sendChunk(",");
    sendPartitionJson("bootPartition", esp_ota_get_boot_partition());
    sendChunk(",");
    sendPartitionJson("nextUpdatePartition", esp_ota_get_next_update_partition(nullptr));
    sendChunk(",");
    sendAppPartitionsJson();
    sendChunk("}");
    endResponse();
}
#endif

} // namespace esp32base_web

#endif
