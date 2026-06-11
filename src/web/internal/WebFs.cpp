#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB && ESP32BASE_ENABLE_FS

#include "WebInternal.h"

#include <time.h>

namespace esp32base_web {

#if ESP32BASE_ENABLE_FS
static const uint8_t FS_TOP_MAX = 10;
static const uint16_t FS_TREE_LIST_LIMIT = 128;
static const uint16_t FS_UPLOAD_DIR_LIMIT = 64;
static const uint8_t FS_SCAN_MAX_DEPTH = 8;
#if ESP32BASE_ENABLE_NTP
static const uint32_t FS_TRUSTED_TIME_MIN_EPOCH = ESP32BASE_NTP_SYNC_MIN_EPOCH;
#else
static const uint32_t FS_TRUSTED_TIME_MIN_EPOCH = 1700000000UL;
#endif

void fsScanYield() {
#if ESP32BASE_ENABLE_WATCHDOG
    Esp32BaseWatchdog::feed();
#endif
    yield();
}

void fsFormatCount(uint32_t count, char* out, size_t len) {
    snprintf(out, len, "%lu", static_cast<unsigned long>(count));
}

bool fsJoinPath(const char* dir, const char* name, char* out, size_t len) {
    if (!out || len == 0) {
        return false;
    }
    int written = 0;
    if (!name || !name[0]) {
        written = snprintf(out, len, "%s", dir && dir[0] ? dir : "/");
    } else if (name[0] == '/') {
        written = snprintf(out, len, "%s", name);
    } else if (!dir || !dir[0] || strcmp(dir, "/") == 0) {
        written = snprintf(out, len, "/%s", name);
    } else {
        written = snprintf(out, len, "%s/%s", dir, name);
    }
    if (written <= 0 || static_cast<size_t>(written) >= len) {
        out[0] = '\0';
        return false;
    }
    return true;
}

void fsAddTopFile(FsScan& scan, const char* path, uint64_t size) {
    uint8_t pos = scan.topCount;
    while (pos > 0 && size > scan.top[pos - 1].size) {
        --pos;
    }
    if (pos >= FS_TOP_MAX) {
        return;
    }
    const uint8_t limit = scan.topCount < FS_TOP_MAX ? scan.topCount : static_cast<uint8_t>(FS_TOP_MAX - 1);
    for (int8_t i = static_cast<int8_t>(limit); i > static_cast<int8_t>(pos); --i) {
        scan.top[i] = scan.top[i - 1];
    }
    if (scan.topCount < FS_TOP_MAX) {
        ++scan.topCount;
    }
    scan.top[pos].size = size;
    strlcpy(scan.top[pos].path, path && path[0] ? path : "-", sizeof(scan.top[pos].path));
}

void sendFsDeleteForm(const char* path) {
    sendChunk("<form method='post' action='/esp32base/fs/delete' onsubmit=\"return confirm('Delete this file? This cannot be undone.')&&once(this)\">");
    sendHiddenInput("path", path && path[0] ? path : "");
    sendChunk("<input class='danger fsdelete' type='submit' value='Delete'></form>");
}

void sendFsDownloadForm(const char* path) {
    sendChunk("<form method='get' action='/esp32base/fs/download'>");
    sendHiddenInput("path", path && path[0] ? path : "");
    sendChunk("<input class='secondary fsaction' type='submit' value='Download'></form>");
}

void sendFsPathTooLongRow(const char* dir, const char* name, bool isDir) {
    sendChunk("<tr><td>");
    sendEscapedHtmlChunk(dir && dir[0] ? dir : "/");
    sendChunk("/");
    sendEscapedHtmlChunk(name && name[0] ? name : "-");
    sendChunk("</td><td>");
    sendEscapedHtmlChunk(isDir ? "dir" : "file");
    sendChunk("</td><td>-</td><td>-</td><td>");
    sendStatusTag(Esp32BaseWeb::UI_WARN, "path too long");
    sendChunk("</td><td>-</td></tr>");
}

#if ESP32BASE_ENABLE_APP_EVENTS
bool appEventsOwnsPath(const char* path) {
    return path && strcmp(path, Esp32BaseAppEventLog::path()) == 0;
}

void sendFsAppEventsTag() {
    sendStatusTag(Esp32BaseWeb::UI_INFO, "app events store");
}
#endif

bool esp32BaseOwnsPath(const char* path) {
    return path && (strcmp(path, "/esp32base") == 0 || strncmp(path, "/esp32base/", 11) == 0);
}

void sendFsEsp32BaseTag() {
    sendStatusTag(Esp32BaseWeb::UI_INFO, "esp32base managed");
}

void fsFormatModifiedTime(uint32_t modifiedEpoch, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (modifiedEpoch < FS_TRUSTED_TIME_MIN_EPOCH) {
        strlcpy(out, "unknown", len);
        return;
    }
    const time_t raw = static_cast<time_t>(modifiedEpoch);
    struct tm tmValue;
    localtime_r(&raw, &tmValue);
    if (strftime(out, len, "%Y-%m-%d %H:%M:%S", &tmValue) == 0) {
        strlcpy(out, "unknown", len);
    }
}

void sendFsFileStatus(const char* path, bool readable) {
#if ESP32BASE_ENABLE_APP_EVENTS
    if (appEventsOwnsPath(path)) {
        sendFsAppEventsTag();
    } else
#endif
#if ESP32BASE_ENABLE_FILELOG
    if (fileLogOwnsPath(path)) {
        sendStatusTag(Esp32BaseWeb::UI_INFO, "filelog");
    } else
#endif
    if (esp32BaseOwnsPath(path)) {
        sendFsEsp32BaseTag();
    } else if (readable) {
        sendStatusTag(Esp32BaseWeb::UI_OK, "ok");
    }
    if (!readable) {
        sendStatusTag(Esp32BaseWeb::UI_WARN, "unreadable");
    }
}

void sendFsDirStatus(const char* path) {
    if (esp32BaseOwnsPath(path)) {
        sendFsEsp32BaseTag();
    } else {
        sendStatusTag(Esp32BaseWeb::UI_OK, "ok");
    }
}

void sendFsFileActions(const char* path, bool manage) {
    sendChunk("<div class='fsactions'>");
    sendFsDownloadForm(path);
    if (manage) {
        sendFsDeleteForm(path);
    }
    sendChunk("</div>");
}

void sendFsUnreadableActions(const char* path, bool manage) {
    sendChunk("<div class='fsactions'>");
    if (manage) {
        sendFsDeleteForm(path);
    } else {
        sendChunk("-");
    }
    sendChunk("</div>");
}

bool fsFileReadableStart(const char* path, size_t size) {
    if (size == 0) {
        return true;
    }
    uint8_t value = 0;
    size_t readLen = 0;
    return Esp32BaseFs::readBytesAt(path, 0, &value, 1, &readLen) && readLen == 1;
}

bool fsFileFullyReadable(const char* path, uint32_t size) {
    if (size == 0) {
        return true;
    }
    uint8_t buffer[512];
    uint32_t offset = 0;
    while (offset < size) {
        const size_t maxLen = size - offset > sizeof(buffer) ? sizeof(buffer) : static_cast<size_t>(size - offset);
        size_t readLen = 0;
        if (!Esp32BaseFs::readBytesAt(path, offset, buffer, maxLen, &readLen) || readLen == 0) {
            return false;
        }
        offset += static_cast<uint32_t>(readLen);
        fsScanYield();
    }
    return offset == size;
}

void sendFsTreeRow(const char* path, size_t size, bool isDir, uint32_t modifiedEpoch, bool manage) {
    char sizeBuf[48];
    char modified[32];
    fsFormatModifiedTime(modifiedEpoch, modified, sizeof(modified));
    sendChunk("<tr><td>");
    sendEscapedHtmlChunk(path && path[0] ? path : "-");
    sendChunk("</td><td>");
    sendEscapedHtmlChunk(isDir ? "dir" : "file");
    sendChunk("</td><td>");
    if (isDir) {
        sendChunk("-");
    } else {
        formatReadableBytes(size, sizeBuf, sizeof(sizeBuf));
        sendEscapedHtmlChunk(sizeBuf);
    }
    sendChunk("</td><td>");
    sendEscapedHtmlChunk(modified);
    sendChunk("</td><td>");
    const bool readable = isDir || fsFileReadableStart(path, size);
    if (isDir) {
        sendFsDirStatus(path);
    } else {
        sendFsFileStatus(path, readable);
    }
    sendChunk("</td><td>");
    if (isDir) {
        sendChunk("-");
    } else if (!readable) {
        sendFsUnreadableActions(path, manage);
    } else {
        sendFsFileActions(path, manage);
    }
    sendChunk("</td></tr>");
}

void fsWalkCallback(const Esp32BaseFs::EntryInfo& entry, void* user);
void fsNoopListCallback(const char*, size_t, bool, void*);

bool fsWalkDir(FsScan& scan, const char* dir, uint8_t depth, bool emitRows, bool manage, uint16_t emitLimit, uint16_t* emittedRows) {
    FsWalkFrame frame = {&scan, dir, depth, emitRows, manage, emitLimit, emittedRows};
    return Esp32BaseFs::listDirInfo(dir, fsWalkCallback, &frame);
}

void fsWalkCallback(const Esp32BaseFs::EntryInfo& entry, void* user) {
    FsWalkFrame* frame = static_cast<FsWalkFrame*>(user);
    if (!frame || !frame->scan) {
        return;
    }
    char path[96];
    const bool pathOk = fsJoinPath(frame->dir, entry.name, path, sizeof(path));
    frame->scan->entries++;
    if (!pathOk) {
        if (entry.isDir) {
            frame->scan->dirs++;
        } else {
            frame->scan->files++;
            frame->scan->listedSize += entry.size;
        }
        if (frame->emitRows && frame->emittedRows && *frame->emittedRows < frame->emitLimit) {
            sendFsPathTooLongRow(frame->dir, entry.name, entry.isDir);
            (*frame->emittedRows)++;
        }
        fsScanYield();
        return;
    }
    if (entry.isDir) {
        frame->scan->dirs++;
    } else {
        frame->scan->files++;
        frame->scan->listedSize += entry.size;
        fsAddTopFile(*frame->scan, path, entry.size);
    }
    if (frame->emitRows && frame->emittedRows && *frame->emittedRows < frame->emitLimit) {
        sendFsTreeRow(path, entry.size, entry.isDir, entry.modifiedEpoch, frame->manage);
        (*frame->emittedRows)++;
    }
    fsScanYield();
    if (entry.isDir && frame->depth + 1 < FS_SCAN_MAX_DEPTH && (!frame->emitRows || !frame->emittedRows || *frame->emittedRows < frame->emitLimit)) {
        fsWalkDir(*frame->scan, path, static_cast<uint8_t>(frame->depth + 1), frame->emitRows, frame->manage, frame->emitLimit, frame->emittedRows);
    }
}

bool scanFs(FsScan& scan) {
    memset(&scan, 0, sizeof(scan));
    return Esp32BaseFs::isReady() && fsWalkDir(scan, "/", 0, false, false, 0, nullptr);
}

bool fsManageMode() {
    return g_server.hasArg("manage") && g_server.arg("manage") != "0";
}

bool fsInternalUsageHigh(const FsScan& scan) {
    const uint64_t used = Esp32BaseFs::usedBytes();
    if (used <= scan.listedSize) {
        return false;
    }
    const uint64_t gap = used - scan.listedSize;
    return gap > 131072ULL && gap > scan.listedSize;
}

bool validFsFilePath(const char* path) {
    return path && path[0] == '/' && path[1] != '\0' && strlen(path) < 96 &&
           !strstr(path, "..") && !strstr(path, "//");
}

bool fsReadArg(const char* name, char* out, size_t len) {
    if (!out || len == 0 || !name || !g_server.hasArg(name)) {
        return false;
    }
    const String value = g_server.arg(name);
    if (value.length() >= len) {
        out[0] = '\0';
        return false;
    }
    strlcpy(out, value.c_str(), len);
    return true;
}

bool fsReadPathArg(const char* name, char* out, size_t len) {
    return fsReadArg(name, out, len) && validFsFilePath(out);
}

bool validFsDeletePath(const char* path) {
    return validFsFilePath(path);
}

bool validFsDirectoryPath(const char* path) {
    return path && ((strcmp(path, "/") == 0) || validFsFilePath(path));
}

bool fsUploadFilenameValid(const char* name) {
    if (!name || !name[0] || strlen(name) > 63 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return false;
    }
    for (const char* p = name; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 32 || c == 127 || *p == '/' || *p == '\\' || *p == ':') {
            return false;
        }
    }
    return !strstr(name, "..");
}

bool fsUploadDirectoryExists(const char* dir) {
    if (!validFsDirectoryPath(dir)) {
        return false;
    }
    if (strcmp(dir, "/") == 0) {
        return Esp32BaseFs::isReady();
    }
    return Esp32BaseFs::listDir(dir, fsNoopListCallback, nullptr);
}

bool fsBuildUploadPath(const char* dir, const char* name, char* out, size_t len) {
    if (!out || len == 0 || !validFsDirectoryPath(dir) || !fsUploadFilenameValid(name)) {
        return false;
    }
    int written = 0;
    if (strcmp(dir, "/") == 0) {
        written = snprintf(out, len, "/%s", name);
    } else {
        written = snprintf(out, len, "%s/%s", dir, name);
    }
    if (written <= 0 || static_cast<size_t>(written) >= len) {
        out[0] = '\0';
        return false;
    }
    return validFsFilePath(out);
}

bool fsBuildUploadTempPath(const char* target, char* out, size_t len) {
    if (!out || len == 0 || !validFsFilePath(target)) {
        return false;
    }
    char dir[96];
    strlcpy(dir, target, sizeof(dir));
    char* slash = strrchr(dir, '/');
    if (!slash) {
        out[0] = '\0';
        return false;
    }
    if (slash == dir) {
        dir[1] = '\0';
    } else {
        *slash = '\0';
    }

    const uint32_t seed = millis();
    for (uint8_t attempt = 0; attempt < 16; ++attempt) {
        const uint32_t token = seed + attempt;
        int written = 0;
        if (strcmp(dir, "/") == 0) {
            written = snprintf(out, len, "/.u%08lx%02x", static_cast<unsigned long>(token), static_cast<unsigned>(attempt));
        } else {
            written = snprintf(out, len, "%s/.u%08lx%02x", dir, static_cast<unsigned long>(token), static_cast<unsigned>(attempt));
        }
        if (written > 0 && static_cast<size_t>(written) < len && validFsFilePath(out) && !Esp32BaseFs::exists(out)) {
            return true;
        }
    }
    out[0] = '\0';
    return false;
}

bool fsRemoveUploadTempIfPresent(const char* tempPath) {
    if (!tempPath || !tempPath[0] || !Esp32BaseFs::exists(tempPath)) {
        return true;
    }
    return Esp32BaseFs::removeFileWithRecovery(tempPath) != Esp32BaseFs::REMOVE_FILE_FAILED;
}

bool fsCommitUploadedTemp(const char* tempPath, const char* targetPath, bool overwrite) {
    if (!validFsFilePath(tempPath) || !validFsFilePath(targetPath) || strcmp(tempPath, targetPath) == 0) {
        return false;
    }
    if (!Esp32BaseFs::exists(tempPath)) {
        return false;
    }
    if (Esp32BaseFs::exists(targetPath) && !overwrite) {
        return false;
    }
    return Esp32BaseFs::rename(tempPath, targetPath);
}

void fsSetUploadError(const char* message) {
    strlcpy(g_fsUploadError, message && message[0] ? message : "upload failed", sizeof(g_fsUploadError));
}

void fsCloseUploadFile() {
    g_fsUploadActive = false;
    fsScanYield();
}

void fsResetUploadState() {
    g_fsUploadForbidden = false;
    g_fsUploadStartFailed = false;
    g_fsUploadReceived = false;
    g_fsUploadModified = false;
    g_fsUploadAppEventsTarget = false;
    g_fsUploadFileLogTarget = false;
    g_fsUploadActive = false;
    g_fsUploadOverwrite = false;
    g_fsUploadBytes = 0;
    g_fsUploadPath[0] = '\0';
    g_fsUploadTempPath[0] = '\0';
    g_fsUploadError[0] = '\0';
}

bool fsReloadUploadRuntime(const char* path, bool appEventsTarget, bool fileLogTarget, const char** errorOut) {
    if (errorOut) {
        *errorOut = nullptr;
    }
    bool ok = true;
    (void)path;
    (void)appEventsTarget;
    (void)fileLogTarget;
#if ESP32BASE_ENABLE_APP_EVENTS
    if (appEventsTarget && !Esp32BaseAppEventLog::reload()) {
        ESP32BASE_LOG_W("web", "fs_upload_reload_failed path=%s target=app_events error=%s", path && path[0] ? path : "-", Esp32BaseAppEventLog::lastError());
        if (errorOut && !*errorOut) {
            *errorOut = "App Events store reload failed";
        }
        ok = false;
    }
#endif
#if ESP32BASE_ENABLE_FILELOG
    if (fileLogTarget && !Esp32BaseFileLog::begin()) {
        ESP32BASE_LOG_W("web", "fs_upload_reload_failed path=%s target=filelog", path && path[0] ? path : "-");
        if (errorOut && !*errorOut) {
            *errorOut = "FileLog reload failed";
        }
        ok = false;
    }
#endif
    return ok;
}

void fsRecoverModifiedUploadRuntime(const char* path, bool modified, bool appEventsTarget, bool fileLogTarget) {
    if (!modified || (!appEventsTarget && !fileLogTarget)) {
        return;
    }
    const char* ignoredError = nullptr;
    fsReloadUploadRuntime(path, appEventsTarget, fileLogTarget, &ignoredError);
}

bool fsUploadTargetIsDirectory(const char* path) {
    return validFsFilePath(path) && Esp32BaseFs::listDir(path, fsNoopListCallback, nullptr);
}

bool fsUploadFileReadableEnd(const char* path, uint64_t size) {
    if (size == 0) {
        return true;
    }
    if (size > UINT32_MAX) {
        return false;
    }
    uint8_t value = 0;
    size_t readLen = 0;
    return Esp32BaseFs::readBytesAt(path,
                                    static_cast<uint32_t>(size - 1U),
                                    &value,
                                    1,
                                    &readLen) &&
           readLen == 1;
}

void fsSendJsonBool(const char* name, bool value, bool comma = true) {
    if (comma) {
        sendChunk(",");
    }
    sendChunk("\"");
    sendChunk(name);
    sendChunk("\":");
    sendChunk(value ? "true" : "false");
}

void fsSendUploadJson(int code, bool ok, const char* error, const char* path, bool exists = false, bool isDir = false) {
    if (!beginResponse(code, "application/json", nullptr)) {
        return;
    }
    sendChunk("{\"ok\":");
    sendChunk(ok ? "true" : "false");
    fsSendJsonBool("exists", exists);
    fsSendJsonBool("directory", isDir);
    if (path && path[0]) {
        sendChunk(",\"path\":\"");
        sendEscapedJsonChunk(path);
        sendChunk("\"");
    }
    if (error && error[0]) {
        sendChunk(",\"error\":\"");
        sendEscapedJsonChunk(error);
        sendChunk("\"");
    }
    sendChunk("}");
    endResponse();
}

void fsUploadDirOptionCallback(const char* name, size_t, bool isDir, void* user);

void sendFsUploadDirectoryOptionsFor(const char* dir, uint8_t depth, uint16_t* emitted, bool* pathTooLong) {
    if (!emitted || *emitted >= FS_UPLOAD_DIR_LIMIT || depth >= FS_SCAN_MAX_DEPTH) {
        return;
    }
    FsUploadDirFrame frame = {dir, depth, emitted, pathTooLong};
    Esp32BaseFs::listDir(dir, fsUploadDirOptionCallback, &frame);
}

void fsUploadDirOptionCallback(const char* name, size_t, bool isDir, void* user) {
    if (!isDir || !user) {
        return;
    }
    FsUploadDirFrame* frame = static_cast<FsUploadDirFrame*>(user);
    if (!frame->emitted || *frame->emitted >= FS_UPLOAD_DIR_LIMIT) {
        return;
    }
    char path[96];
    if (!fsJoinPath(frame->dir, name, path, sizeof(path))) {
        if (frame->pathTooLong) {
            *frame->pathTooLong = true;
        }
        sendChunk("<option disabled>path too long: ");
        sendEscapedHtmlChunk(frame->dir && frame->dir[0] ? frame->dir : "/");
        sendChunk("/");
        sendEscapedHtmlChunk(name && name[0] ? name : "-");
        sendChunk("</option>");
        (*frame->emitted)++;
        fsScanYield();
        return;
    }
    if (!validFsDirectoryPath(path)) {
        return;
    }
    sendChunk("<option value='");
    sendEscapedHtmlChunk(path);
    sendChunk("'>");
    sendEscapedHtmlChunk(path);
    sendChunk("</option>");
    (*frame->emitted)++;
    fsScanYield();
    sendFsUploadDirectoryOptionsFor(path, static_cast<uint8_t>(frame->depth + 1), frame->emitted, frame->pathTooLong);
}

void sendFsUploadPanel() {
    sendChunk("<section class='panel formpanel uploadpanel'><h2>Upload file</h2><form id='fsu' class='editform'><div class='fieldgrid'><div class='field short'><label for='fsudir'>Device directory</label><select id='fsudir' name='dir'><option value='/'>/</option>");
    uint16_t emitted = 1;
    bool pathTooLong = false;
    sendFsUploadDirectoryOptionsFor("/", 0, &emitted, &pathTooLong);
    sendChunk("</select></div><div class='field long'><label for='fsufile'>File</label><input id='fsufile' type='file' name='file' required></div><div class='field full'><small>Uses local filename; directory must exist.");
    if (pathTooLong) {
        sendChunk(" Some directories are hidden because their paths are too long.");
    }
    sendChunk("</small></div></div><div class='actions'><input type='submit' value='Upload'></div></form><progress id='fsup' value='0' max='100' style='width:100%;display:none'></progress><p id='fsus' class='statusline muted'></p></section>");
    sendChunk("<script>function fsUH(n){var u=['B','KB','MB','GB'],i=0,x=n;while(x>=1024&&i<u.length-1){x/=1024;i++;}return (i?x.toFixed(2):Math.round(x))+' '+u[i];}function fsUEnc(s){return encodeURIComponent(s);}function fsUFail(t){var f=document.getElementById('fsu'),b=f.querySelector('[type=submit]');document.getElementById('fsus').textContent=t;f.dataset.busy='';if(b)b.disabled=false;}function fsUSend(dir,file,ow){var f=document.getElementById('fsu'),st=document.getElementById('fsus'),pg=document.getElementById('fsup'),b=f.querySelector('[type=submit]'),d=new FormData(),x=new XMLHttpRequest();d.append('file',file,file.name);if(b)b.disabled=true;pg.style.display='block';st.textContent='Uploading 0%';x.upload.onprogress=function(ev){if(ev.lengthComputable){var p=Math.floor(ev.loaded*100/ev.total);pg.value=p;st.textContent='Uploading '+p+'% '+fsUH(ev.loaded)+' / '+fsUH(ev.total);}};x.onload=function(){var r={};try{r=JSON.parse(x.responseText||'{}');}catch(e){}if(x.status==200&&r.ok){location.href=r.redirect||'/esp32base/fs?manage=1&uploaded=1';return;}fsUFail('Upload failed: '+(r.error||('HTTP '+x.status)));};x.onerror=function(){fsUFail('Upload failed: network error');};x.open('POST','/esp32base/fs/upload?dir='+fsUEnc(dir)+(ow?'&overwrite=1':''));x.send(d);}document.getElementById('fsu').onsubmit=function(e){e.preventDefault();if(this.dataset.busy)return false;this.dataset.busy=1;var dir=document.getElementById('fsudir').value,file=document.getElementById('fsufile').files[0],st=document.getElementById('fsus'),b=this.querySelector('[type=submit]');if(!file){this.dataset.busy='';return false;}if(b)b.disabled=true;st.textContent='Checking target...';var x=new XMLHttpRequest();x.onload=function(){var r={};try{r=JSON.parse(x.responseText||'{}');}catch(e){}if(x.status!=200||!r.ok){fsUFail('Upload blocked: '+(r.error||('HTTP '+x.status)));return;}var ow=false;if(r.exists){ow=confirm((r.path||file.name)+' already exists. Overwrite?');if(!ow){fsUFail('Upload canceled');return;}}fsUSend(dir,file,ow);};x.onerror=function(){fsUFail('Upload blocked: network error');};x.open('GET','/esp32base/fs/check?dir='+fsUEnc(dir)+'&name='+fsUEnc(file.name));x.send();return false;};</script>");
}

void fsDownloadFilename(const char* path, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    const char* name = path ? strrchr(path, '/') : nullptr;
    name = name && name[1] ? name + 1 : "littlefs.bin";
    size_t used = 0;
    for (const char* p = name; *p && used + 1 < len && used < 63; ++p) {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        out[used++] = ok ? c : '_';
    }
    out[used] = '\0';
    if (!validDownloadFilename(out)) {
        strlcpy(out, "littlefs.bin", len);
    }
}

void fsNoopListCallback(const char*, size_t, bool, void*) {
}

#if ESP32BASE_ENABLE_FILELOG
bool fileLogOwnsPath(const char* path) {
    if (!path || !path[0]) {
        return false;
    }
    if (strcmp(path, Esp32BaseFileLog::path()) == 0) {
        return true;
    }
    char segment[Esp32BaseFileLog::SEGMENT_PATH_BUFFER_SIZE];
    for (uint8_t i = 0; i < Esp32BaseFileLog::rotateFiles(); ++i) {
        if (Esp32BaseFileLog::segmentPath(i, segment, sizeof(segment)) && strcmp(path, segment) == 0) {
            return true;
        }
    }
    return false;
}
#endif

void sendFsInventoryValue(const FsScan& scan, uint64_t fsUsed) {
    char files[16];
    char dirs[16];
    char listed[48];
    char other[48];
    fsFormatCount(scan.files, files, sizeof(files));
    fsFormatCount(scan.dirs, dirs, sizeof(dirs));
    formatReadableBytes(scan.listedSize, listed, sizeof(listed));
    const uint64_t overheadBytes = fsUsed > scan.listedSize ? fsUsed - scan.listedSize : 0;
    formatReadableBytes(overheadBytes, other, sizeof(other));
    sendSubmetricsStart();
    sendSubmetric("Files", files);
    sendSubmetric("Dirs", dirs);
    sendSubmetric("Listed size", listed);
    sendSubmetric("Other/overhead", other);
    sendSubmetricsEnd();
}

void sendFsSummaryCell(const char* label, const char* value) {
    sendChunk("<td><b>");
    sendEscapedHtmlChunk(label);
    sendChunk("</b><em>");
    sendEscapedHtmlChunk(value);
    sendChunk("</em></td>");
}

void sendFsSummaryTable(const FsScan& scan) {
    char used[48];
    char total[48];
    char freeBytes[48];
    char files[16];
    char dirs[16];
    char listed[48];
    char other[48];
    const size_t fsUsed = Esp32BaseFs::usedBytes();
    const size_t fsFree = Esp32BaseFs::freeBytes();
    formatReadableBytes(fsUsed, used, sizeof(used));
    formatReadableBytes(Esp32BaseFs::totalBytes(), total, sizeof(total));
    formatReadableBytes(fsFree, freeBytes, sizeof(freeBytes));
    fsFormatCount(scan.files, files, sizeof(files));
    fsFormatCount(scan.dirs, dirs, sizeof(dirs));
    formatReadableBytes(scan.listedSize, listed, sizeof(listed));
    const uint64_t overheadBytes = fsUsed > scan.listedSize ? fsUsed - scan.listedSize : 0;
    formatReadableBytes(overheadBytes, other, sizeof(other));

    sendChunk("<table class='fsummary'><tr><th>FS</th><td>");
    sendStatusTag(fsFree == 0 ? Esp32BaseWeb::UI_WARN : Esp32BaseWeb::UI_OK, fsFree == 0 ? "full" : "ready");
    sendChunk("</td>");
    sendFsSummaryCell("Used", used);
    sendFsSummaryCell("Free", freeBytes);
    sendFsSummaryCell("Total", total);
    sendChunk("</tr><tr><th>File inventory</th>");
    sendFsSummaryCell("Files", files);
    sendFsSummaryCell("Dirs", dirs);
    sendFsSummaryCell("Listed size", listed);
    sendFsSummaryCell("Other/overhead", other);
    sendChunk("</tr></table>");
}

void sendFsSummaryRows(const FsScan& scan) {
    char used[48];
    char total[48];
    char freeBytes[48];
    const size_t fsUsed = Esp32BaseFs::usedBytes();
    const size_t fsFree = Esp32BaseFs::freeBytes();
    formatReadableBytes(fsUsed, used, sizeof(used));
    formatReadableBytes(Esp32BaseFs::totalBytes(), total, sizeof(total));
    formatReadableBytes(fsFree, freeBytes, sizeof(freeBytes));
    sendInfoRowStart("FS");
    sendStatusTag(fsFree == 0 ? Esp32BaseWeb::UI_WARN : Esp32BaseWeb::UI_OK, fsFree == 0 ? "full" : "ready");
    sendChunk("<a class='btnlink compact' href='/esp32base/fs'>Details</a>");
    sendSubmetricsStart();
    sendSubmetric("Used", used);
    sendSubmetric("Free", freeBytes);
    sendSubmetric("Total", total);
    sendSubmetricsEnd();
    sendInfoRowEnd();
    sendInfoRowStart("File inventory");
    sendFsInventoryValue(scan, fsUsed);
    sendInfoRowEnd();
}
#endif

#if ESP32BASE_ENABLE_FS
void handleFsPage() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    const bool manage = fsManageMode();
    Esp32BaseWeb::sendHeader("File system");
    Esp32BaseWeb::sendPageTitle("File system", "Read-only LittleFS inventory and storage usage.");
    if (g_server.hasArg("uploaded")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "File uploaded");
    } else if (g_server.hasArg("deleted")) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_OK, "File deleted or cleared");
    } else if (g_server.hasArg("error")) {
        const String error = g_server.arg("error");
        const char* message = error == "delete_failed" ?
            "LittleFS could not remove or clear the file. For FileLog files, use Clear system logs or format LittleFS if storage remains full." :
            error.c_str();
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_DANGER, "File action failed", message);
    }
    if (!Esp32BaseFs::isReady()) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "LittleFS unavailable");
        Esp32BaseWeb::sendFooter();
        return;
    }
    FsScan scan;
    if (!scanFs(scan)) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "LittleFS scan failed");
        Esp32BaseWeb::sendFooter();
        return;
    }
    if (fsInternalUsageHigh(scan)) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "Internal FS usage is high", "Visible files are much smaller than FS used. Delete may not fully recover space; download files you need and format LittleFS if free space stays low.");
    }
    if (manage) {
        Esp32BaseWeb::sendNotice(Esp32BaseWeb::UI_WARN, "File management mode", "Only single-file delete is available. Directories, batch actions and edit are intentionally not provided.");
        sendChunk("<p class='statusline'><a class='btnlink secondary' href='/esp32base/fs'>View only</a></p>");
        sendFsUploadPanel();
    } else {
        sendChunk("<p class='statusline'><a class='btnlink warn' href='/esp32base/fs?manage=1'>Manage files</a></p>");
    }
    sendChunk("<section class='panel statuspage'><h2>Summary</h2><div class='tablewrap'>");
    sendFsSummaryTable(scan);
    sendChunk("</div></section>");

    sendChunk("<section class='panel statuspage'><h2>Largest files</h2><div class='tablewrap'><table class='part'><tr><th>Path</th><th>Size</th></tr>");
    if (scan.topCount == 0) {
        sendChunk("<tr><td colspan='2'>No files</td></tr>");
    } else {
        char sizeBuf[48];
        const uint8_t count = scan.topCount < FS_TOP_MAX ? scan.topCount : FS_TOP_MAX;
        for (uint8_t i = 0; i < count; ++i) {
            formatReadableBytes(scan.top[i].size, sizeBuf, sizeof(sizeBuf));
            sendChunk("<tr><td>");
            sendEscapedHtmlChunk(scan.top[i].path);
            sendChunk("</td><td>");
            sendEscapedHtmlChunk(sizeBuf);
            sendChunk("</td></tr>");
        }
    }
    sendChunk("</table></div></section>");

    sendChunk("<section class='panel statuspage'><h2>File tree</h2><div class='tablewrap'><table class='part'><tr><th>Path</th><th>Type</th><th>Size</th><th>Last modified</th><th>Status</th><th>Action</th></tr>");
    FsScan treeScan;
    memset(&treeScan, 0, sizeof(treeScan));
    uint16_t emittedRows = 0;
    fsWalkDir(treeScan, "/", 0, true, manage, FS_TREE_LIST_LIMIT, &emittedRows);
    if (emittedRows == 0) {
        sendChunk("<tr><td colspan='6'>No files</td></tr>");
    }
    sendChunk("</table></div>");
    if (scan.entries > FS_TREE_LIST_LIMIT) {
        sendChunk("<p class='notice warn'>File tree truncated at 128 entries.</p>");
    }
    sendChunk("</section>");
    Esp32BaseWeb::sendFooter();
}

void handleFsDownloadGet() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    if (!Esp32BaseFs::isReady()) {
        g_server.send(404, "text/plain", "File system unavailable");
        return;
    }
    char path[96];
    if (!fsReadPathArg("path", path, sizeof(path))) {
        g_server.send(400, "text/plain", "Invalid path");
        return;
    }
    if (Esp32BaseFs::listDir(path, fsNoopListCallback, nullptr)) {
        g_server.send(400, "text/plain", "Directory download is not supported");
        return;
    }
    const int64_t fileSize = Esp32BaseFs::fileSize(path);
    if (fileSize < 0) {
        g_server.send(404, "text/plain", "File not found");
        return;
    }
    if (static_cast<uint64_t>(fileSize) > static_cast<uint64_t>(UINT32_MAX)) {
        g_server.send(413, "text/plain", "File too large");
        return;
    }
    const uint32_t size = static_cast<uint32_t>(fileSize);

    if (!fsFileFullyReadable(path, size)) {
        ESP32BASE_LOG_W("web", "fs_download_read_failed path=%s stage=preflight", path);
        g_server.send(500, "text/plain", "File read failed");
        return;
    }

    char filename[64];
    fsDownloadFilename(path, filename, sizeof(filename));
    sendResponseHeader("Cache-Control", "no-store");
    if (!beginResponse(200, "application/octet-stream", filename)) {
        g_server.send(500, "text/plain", "Download failed");
        return;
    }

    uint8_t buffer[512];
    size_t sent = 0;
    while (sent < size && !g_responseBroken) {
        const size_t maxLen = size - sent > sizeof(buffer) ? sizeof(buffer) : size - sent;
        size_t readLen = 0;
        if (!Esp32BaseFs::readBytesAt(path, static_cast<uint32_t>(sent), buffer, maxLen, &readLen) || readLen == 0) {
            ESP32BASE_LOG_W("web", "fs_download_read_failed path=%s offset=%lu", path, static_cast<unsigned long>(sent));
            break;
        }
        sendChunk(reinterpret_cast<const char*>(buffer), readLen);
        sent += readLen;
        fsScanYield();
    }
    endResponse();
    ESP32BASE_LOG_I("web", "fs_download_requested path=%s bytes=%lu", path, static_cast<unsigned long>(sent));
}

void handleFsCheckGet() {
    markRequest();
    if (!ensureAuth()) {
        return;
    }
    if (!Esp32BaseFs::isReady()) {
        fsSendUploadJson(503, false, "File system unavailable", nullptr);
        return;
    }
    char dir[96];
    char name[64];
    char path[96];
    if (!fsReadArg("dir", dir, sizeof(dir)) || !fsReadArg("name", name, sizeof(name))) {
        fsSendUploadJson(400, false, "Invalid filename or target path", nullptr);
        return;
    }
    if (!fsUploadDirectoryExists(dir)) {
        fsSendUploadJson(400, false, "Target directory missing", nullptr);
        return;
    }
    if (!fsBuildUploadPath(dir, name, path, sizeof(path))) {
        fsSendUploadJson(400, false, "Invalid filename or target path", nullptr);
        return;
    }
    if (fsUploadTargetIsDirectory(path)) {
        fsSendUploadJson(400, false, "Target is a directory", path, false, true);
        return;
    }
    const bool exists = Esp32BaseFs::fileSize(path) >= 0;
    fsSendUploadJson(200, true, nullptr, path, exists, false);
}

void handleFsUploadDone() {
    markRequest();
    if (!ensureAuth()) {
        fsResetUploadState();
        return;
    }
    const bool uploadReceived = g_fsUploadReceived;
    const bool forbidden = g_fsUploadForbidden;
    const bool startFailed = g_fsUploadStartFailed;
    const bool overwrite = g_fsUploadOverwrite;
    const bool uploadAppEventsTarget = g_fsUploadAppEventsTarget;
    const bool uploadFileLogTarget = g_fsUploadFileLogTarget;
    const size_t uploadBytes = g_fsUploadBytes;
    char uploadPath[96];
    char uploadTempPath[96];
    char uploadError[96];
    strlcpy(uploadPath, g_fsUploadPath, sizeof(uploadPath));
    strlcpy(uploadTempPath, g_fsUploadTempPath, sizeof(uploadTempPath));
    strlcpy(uploadError, g_fsUploadError, sizeof(uploadError));
    fsCloseUploadFile();
    if (forbidden || !requestSameOrigin()) {
        ESP32BASE_LOG_W("web", "post_rejected context=fs_upload reason=cross_origin");
        fsResetUploadState();
        fsSendUploadJson(403, false, "forbidden", uploadPath);
        return;
    }
    if (startFailed || uploadError[0]) {
        ESP32BASE_LOG_W("web", "fs_upload_rejected path=%s error=%s", uploadPath[0] ? uploadPath : "-", uploadError[0] ? uploadError : "upload rejected");
        fsRemoveUploadTempIfPresent(uploadTempPath);
        fsRecoverModifiedUploadRuntime(uploadPath, false, uploadAppEventsTarget, uploadFileLogTarget);
        fsResetUploadState();
        fsSendUploadJson(400, false, uploadError[0] ? uploadError : "upload rejected", uploadPath);
        return;
    }
    if (!uploadReceived || !uploadPath[0]) {
        ESP32BASE_LOG_W("web", "fs_upload_rejected reason=no_upload");
        fsResetUploadState();
        fsSendUploadJson(400, false, "No upload received", nullptr);
        return;
    }
    const int64_t actualSize = uploadTempPath[0] ? Esp32BaseFs::fileSize(uploadTempPath) : -1;
    if (actualSize < 0 ||
        static_cast<uint64_t>(actualSize) != uploadBytes ||
        !fsUploadFileReadableEnd(uploadTempPath, static_cast<uint64_t>(actualSize))) {
        fsRemoveUploadTempIfPresent(uploadTempPath);
        fsRecoverModifiedUploadRuntime(uploadPath, false, uploadAppEventsTarget, uploadFileLogTarget);
        fsResetUploadState();
        fsSendUploadJson(500, false, "Upload verification failed", uploadPath);
        return;
    }
    if (!fsCommitUploadedTemp(uploadTempPath, uploadPath, overwrite)) {
        fsRemoveUploadTempIfPresent(uploadTempPath);
        fsRecoverModifiedUploadRuntime(uploadPath, false, uploadAppEventsTarget, uploadFileLogTarget);
        fsResetUploadState();
        fsSendUploadJson(500, false, "Upload commit failed", uploadPath);
        return;
    }
    const char* reloadError = nullptr;
#if ESP32BASE_ENABLE_FILELOG
    const bool reloadFileLog = uploadFileLogTarget || Esp32BaseFileLog::faulted();
#else
    const bool reloadFileLog = uploadFileLogTarget;
#endif
    if (!fsReloadUploadRuntime(uploadPath, uploadAppEventsTarget, reloadFileLog, &reloadError)) {
        fsResetUploadState();
        fsSendUploadJson(500, false, reloadError ? reloadError : "Runtime reload failed", uploadPath);
        return;
    }
    {
        ESP32BASE_LOG_W("web", "fs_upload_completed path=%s bytes=%lu overwrite=%s",
                        uploadPath,
                        static_cast<unsigned long>(uploadBytes),
                        overwrite ? "yes" : "no");
        fsResetUploadState();
        if (beginResponse(200, "application/json", nullptr)) {
            sendChunk("{\"ok\":true,\"redirect\":\"/esp32base/fs?manage=1&uploaded=1\",\"path\":\"");
            sendEscapedJsonChunk(uploadPath);
            sendChunk("\"}");
            endResponse();
        }
        return;
    }
}

void handleFsUpload() {
    if (!isAuthenticated()) {
        if (g_server.upload().status == UPLOAD_FILE_START) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("unauthorized");
        }
        return;
    }
    HTTPUpload& upload = g_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        fsResetUploadState();
        g_fsUploadReceived = true;
        g_fsUploadOverwrite = g_server.hasArg("overwrite") && g_server.arg("overwrite") == "1";
        if (!requestSameOrigin()) {
            g_fsUploadForbidden = true;
            g_fsUploadStartFailed = true;
            fsSetUploadError("forbidden");
            ESP32BASE_LOG_W("web", "post_rejected context=fs_upload reason=cross_origin");
            return;
        }
        if (!Esp32BaseFs::isReady()) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("File system unavailable");
            return;
        }
        char dir[96];
        if (!fsReadArg("dir", dir, sizeof(dir))) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("Invalid filename or target path");
            return;
        }
        if (!fsUploadDirectoryExists(dir)) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("Target directory missing");
            return;
        }
        if (!fsBuildUploadPath(dir, upload.filename.c_str(), g_fsUploadPath, sizeof(g_fsUploadPath))) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("Invalid filename or target path");
            return;
        }
#if ESP32BASE_ENABLE_APP_EVENTS
        g_fsUploadAppEventsTarget = appEventsOwnsPath(g_fsUploadPath);
#endif
#if ESP32BASE_ENABLE_FILELOG
        g_fsUploadFileLogTarget = fileLogOwnsPath(g_fsUploadPath);
#endif
        if (fsUploadTargetIsDirectory(g_fsUploadPath)) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("Target is a directory");
            return;
        }
        if (!fsBuildUploadTempPath(g_fsUploadPath, g_fsUploadTempPath, sizeof(g_fsUploadTempPath))) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("Could not allocate upload temp file");
            return;
        }
#if ESP32BASE_ENABLE_FILELOG
        if (g_fsUploadFileLogTarget && Esp32BaseFileLog::isEnabled()) {
            Esp32BaseFileLog::flush();
        }
#endif
        const bool exists = Esp32BaseFs::fileSize(g_fsUploadPath) >= 0;
        if (exists && !g_fsUploadOverwrite) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("File exists");
            return;
        }
        g_fsUploadModified = true;
        if (!Esp32BaseFs::writeBytes(g_fsUploadTempPath, nullptr, 0)) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("Could not open upload temp file");
            return;
        }
        g_fsUploadActive = true;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (g_fsUploadForbidden || g_fsUploadStartFailed || !g_fsUploadActive) {
            return;
        }
        if (!Esp32BaseFs::appendBytes(g_fsUploadTempPath, upload.buf, upload.currentSize)) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("File write failed");
            fsCloseUploadFile();
            return;
        }
        g_fsUploadBytes += upload.currentSize;
        fsScanYield();
    } else if (upload.status == UPLOAD_FILE_END) {
        if (g_fsUploadForbidden || g_fsUploadStartFailed || !g_fsUploadActive) {
            return;
        }
        fsCloseUploadFile();
        if (upload.totalSize > 0 && g_fsUploadBytes != upload.totalSize) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("Upload size mismatch");
            return;
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        g_fsUploadStartFailed = true;
        fsSetUploadError("Upload aborted");
        fsCloseUploadFile();
    }
}

void handleFsDeletePost() {
    markRequest();
    if (!ensurePostAllowed("fs_delete")) {
        return;
    }
    if (!Esp32BaseFs::isReady()) {
        redirectSeeOther("/esp32base/fs?manage=1&error=fs_unavailable");
        return;
    }
    char path[96];
    if (!fsReadPathArg("path", path, sizeof(path))) {
        ESP32BASE_LOG_W("web", "fs_delete_rejected reason=invalid_path");
        redirectSeeOther("/esp32base/fs?manage=1&error=delete_invalid");
        return;
    }
    if (Esp32BaseFs::listDir(path, fsNoopListCallback, nullptr)) {
        ESP32BASE_LOG_W("web", "fs_delete_rejected path=%s reason=directory", path);
        redirectSeeOther("/esp32base/fs?manage=1&error=delete_directory");
        return;
    }
    if (Esp32BaseFs::fileSize(path) < 0) {
        ESP32BASE_LOG_W("web", "fs_delete_rejected path=%s reason=missing", path);
        redirectSeeOther("/esp32base/fs?manage=1&error=delete_missing");
        return;
    }
#if ESP32BASE_ENABLE_APP_EVENTS
    const bool targetIsAppEvents = appEventsOwnsPath(path);
#endif
#if ESP32BASE_ENABLE_FILELOG
    const bool targetIsFileLog = fileLogOwnsPath(path);
    const bool retryFileLogAfterDelete = Esp32BaseFileLog::faulted();
    if (targetIsFileLog && Esp32BaseFileLog::isEnabled()) {
        ESP32BASE_LOG_W("web", "fs_delete_requested path=%s target=filelog", path);
        Esp32BaseFileLog::flush();
    }
#endif
    const Esp32BaseFs::RemoveFileResult removeResult = Esp32BaseFs::removeFileWithRecovery(path);
    bool ok = removeResult == Esp32BaseFs::REMOVE_FILE_DELETED ||
              removeResult == Esp32BaseFs::REMOVE_FILE_CLEARED;
    const bool deleteOk = ok;
    const bool fileClearedOnly = removeResult == Esp32BaseFs::REMOVE_FILE_CLEARED;
#if ESP32BASE_ENABLE_FILELOG
    if (!targetIsFileLog) {
        ESP32BASE_LOG_W("web", "fs_delete_requested path=%s result=%s", path,
                        removeResult == Esp32BaseFs::REMOVE_FILE_DELETED ? "deleted" :
                        (fileClearedOnly ? "cleared" : "failed"));
    }
    if (deleteOk && (targetIsFileLog || retryFileLogAfterDelete)) {
        const bool reloadOk = Esp32BaseFileLog::begin();
        ESP32BASE_LOG_W("web", "fs_delete_reload path=%s target=filelog result=%s", path, reloadOk ? "success" : "failed");
        if (!reloadOk) {
            ok = false;
        }
    }
#else
    ESP32BASE_LOG_W("web", "fs_delete_requested path=%s result=%s", path, ok ? "success" : "failed");
#endif
#if ESP32BASE_ENABLE_APP_EVENTS
    if (deleteOk && targetIsAppEvents) {
        const bool reloadOk = fileClearedOnly
            ? Esp32BaseAppEventLog::clear()
            : Esp32BaseAppEventLog::reload();
        if (!reloadOk) {
            ok = false;
        }
    }
#endif
    redirectSeeOther(ok ? (fileClearedOnly ? "/esp32base/fs?manage=1&deleted=cleared" : "/esp32base/fs?manage=1&deleted=1")
                        : "/esp32base/fs?manage=1&error=delete_failed");
}
#endif

} // namespace esp32base_web

#endif
