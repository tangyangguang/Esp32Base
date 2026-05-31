#include "../../Esp32BaseProfile.h"

#if ESP32BASE_ENABLE_WEB && ESP32BASE_ENABLE_FS

#include "WebInternal.h"

namespace esp32base_web {

#if ESP32BASE_ENABLE_FS
static const uint8_t FS_TOP_MAX = 10;
static const uint16_t FS_TREE_LIST_LIMIT = 128;
static const uint16_t FS_UPLOAD_DIR_LIMIT = 64;
static const uint8_t FS_SCAN_MAX_DEPTH = 8;

void fsScanYield() {
#if ESP32BASE_ENABLE_WATCHDOG
    Esp32BaseWatchdog::feed();
#endif
    yield();
}

void fsFormatCount(uint32_t count, char* out, size_t len) {
    snprintf(out, len, "%lu", static_cast<unsigned long>(count));
}

void fsJoinPath(const char* dir, const char* name, char* out, size_t len) {
    if (!out || len == 0) {
        return;
    }
    if (!name || !name[0]) {
        strlcpy(out, dir && dir[0] ? dir : "/", len);
        return;
    }
    if (name[0] == '/') {
        strlcpy(out, name, len);
        return;
    }
    if (!dir || !dir[0] || strcmp(dir, "/") == 0) {
        snprintf(out, len, "/%s", name);
        return;
    }
    snprintf(out, len, "%s/%s", dir, name);
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
    sendStatusTag(Esp32BaseWeb::UI_WARN, "unreadable");
    if (manage) {
        sendFsDeleteForm(path);
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

void sendFsTreeRow(const char* path, size_t size, bool isDir, bool manage) {
    char sizeBuf[48];
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
    if (isDir) {
        sendChunk("-");
    } else if (!fsFileReadableStart(path, size)) {
        sendFsUnreadableActions(path, manage);
    } else {
        sendFsFileActions(path, manage);
    }
    sendChunk("</td></tr>");
}

void fsWalkCallback(const char* name, size_t size, bool isDir, void* user);
void fsNoopListCallback(const char*, size_t, bool, void*);

bool fsWalkDir(FsScan& scan, const char* dir, uint8_t depth, bool emitRows, bool manage, uint16_t emitLimit, uint16_t* emittedRows) {
    FsWalkFrame frame = {&scan, dir, depth, emitRows, manage, emitLimit, emittedRows};
    return Esp32BaseFs::listDir(dir, fsWalkCallback, &frame);
}

void fsWalkCallback(const char* name, size_t size, bool isDir, void* user) {
    FsWalkFrame* frame = static_cast<FsWalkFrame*>(user);
    if (!frame || !frame->scan) {
        return;
    }
    char path[96];
    fsJoinPath(frame->dir, name, path, sizeof(path));
    frame->scan->entries++;
    if (isDir) {
        frame->scan->dirs++;
    } else {
        frame->scan->files++;
        frame->scan->listedSize += size;
        fsAddTopFile(*frame->scan, path, size);
    }
    if (frame->emitRows && frame->emittedRows && *frame->emittedRows < frame->emitLimit) {
        sendFsTreeRow(path, size, isDir, frame->manage);
        (*frame->emittedRows)++;
    }
    fsScanYield();
    if (isDir && frame->depth + 1 < FS_SCAN_MAX_DEPTH && (!frame->emitRows || !frame->emittedRows || *frame->emittedRows < frame->emitLimit)) {
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

void fsSetUploadError(const char* message) {
    strlcpy(g_fsUploadError, message && message[0] ? message : "upload failed", sizeof(g_fsUploadError));
}

void fsCloseUploadFile() {
    if (g_fsUploadActive) {
        g_fsUploadFile.flush();
        g_fsUploadFile.close();
    }
    g_fsUploadActive = false;
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

void sendFsUploadDirectoryOptionsFor(const char* dir, uint8_t depth, uint16_t* emitted) {
    if (!emitted || *emitted >= FS_UPLOAD_DIR_LIMIT || depth >= FS_SCAN_MAX_DEPTH) {
        return;
    }
    FsUploadDirFrame frame = {dir, depth, emitted};
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
    fsJoinPath(frame->dir, name, path, sizeof(path));
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
    sendFsUploadDirectoryOptionsFor(path, static_cast<uint8_t>(frame->depth + 1), frame->emitted);
}

void sendFsUploadPanel() {
    sendChunk("<section class='panel formpanel uploadpanel'><h2>Upload file</h2><form id='fsu' class='editform'><div class='fieldgrid'><div class='field med'><label for='fsudir'>Device directory</label><select id='fsudir' name='dir'><option value='/'>/</option>");
    uint16_t emitted = 1;
    sendFsUploadDirectoryOptionsFor("/", 0, &emitted);
    sendChunk("</select><small>Upload uses the local filename. Directories must already exist.</small></div><div class='field long'><label for='fsufile'>File</label><input id='fsufile' type='file' name='file' required></div></div><div class='actions'><input type='submit' value='Upload'></div></form><progress id='fsup' value='0' max='100' style='width:100%;display:none'></progress><p id='fsus' class='statusline muted'></p></section>");
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
    char segment[64];
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
            "LittleFS could not remove or clear the file. For FileLog files, use Clear logs or format LittleFS if storage remains full." :
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

    sendChunk("<section class='panel statuspage'><h2>File tree</h2><div class='tablewrap'><table class='part'><tr><th>Path</th><th>Type</th><th>Size</th><th>Action</th></tr>");
    FsScan treeScan;
    memset(&treeScan, 0, sizeof(treeScan));
    uint16_t emittedRows = 0;
    fsWalkDir(treeScan, "/", 0, true, manage, FS_TREE_LIST_LIMIT, &emittedRows);
    if (emittedRows == 0) {
        sendChunk("<tr><td colspan='4'>No files</td></tr>");
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
    strlcpy(path, g_server.hasArg("path") ? g_server.arg("path").c_str() : "", sizeof(path));
    if (!validFsFilePath(path)) {
        g_server.send(400, "text/plain", "Invalid path");
        return;
    }
    if (Esp32BaseFs::listDir(path, fsNoopListCallback, nullptr)) {
        g_server.send(400, "text/plain", "Directory download is not supported");
        return;
    }
    File file = LittleFS.open(path, "r");
    if (!file) {
        g_server.send(404, "text/plain", "File not found");
        return;
    }
    if (file.isDirectory()) {
        file.close();
        g_server.send(400, "text/plain", "Directory download is not supported");
        return;
    }
    const size_t size = file.size();
    if (size > static_cast<size_t>(UINT32_MAX)) {
        file.close();
        g_server.send(413, "text/plain", "File too large");
        return;
    }

    uint8_t buffer[512];
    const size_t firstLen = size > 0 ? file.readBytes(reinterpret_cast<char*>(buffer), size > sizeof(buffer) ? sizeof(buffer) : size) : 0;
    if (size > 0 && firstLen == 0) {
        file.close();
        ESP32BASE_LOG_W("web", "fs_download_read_failed path=%s offset=0", path);
        g_server.send(500, "text/plain", "File read failed");
        return;
    }

    char filename[64];
    fsDownloadFilename(path, filename, sizeof(filename));
    sendResponseHeader("Cache-Control", "no-store");
    if (!beginResponse(200, "application/octet-stream", filename)) {
        file.close();
        g_server.send(500, "text/plain", "Download failed");
        return;
    }

    size_t sent = 0;
    if (firstLen > 0) {
        sendChunk(reinterpret_cast<const char*>(buffer), firstLen);
        sent = firstLen;
    }
    while (sent < size && !g_responseBroken) {
        const size_t maxLen = size - sent > sizeof(buffer) ? sizeof(buffer) : size - sent;
        const size_t readLen = file.readBytes(reinterpret_cast<char*>(buffer), maxLen);
        if (readLen == 0) {
            ESP32BASE_LOG_W("web", "fs_download_read_failed path=%s offset=%lu", path, static_cast<unsigned long>(sent));
            break;
        }
        sendChunk(reinterpret_cast<const char*>(buffer), readLen);
        sent += readLen;
        fsScanYield();
    }
    file.close();
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
    strlcpy(dir, g_server.hasArg("dir") ? g_server.arg("dir").c_str() : "", sizeof(dir));
    strlcpy(name, g_server.hasArg("name") ? g_server.arg("name").c_str() : "", sizeof(name));
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
        return;
    }
    if (g_fsUploadForbidden || !requestSameOrigin()) {
        ESP32BASE_LOG_W("web", "post_rejected context=fs_upload reason=cross_origin");
        g_fsUploadForbidden = false;
        g_fsUploadStartFailed = false;
        fsCloseUploadFile();
        fsSendUploadJson(403, false, "forbidden", g_fsUploadPath);
        return;
    }
    g_fsUploadForbidden = false;
    const bool startFailed = g_fsUploadStartFailed;
    g_fsUploadStartFailed = false;
    fsCloseUploadFile();
#if ESP32BASE_ENABLE_FILELOG
    if (g_fsUploadPath[0] && (fileLogOwnsPath(g_fsUploadPath) || Esp32BaseFileLog::faulted())) {
        Esp32BaseFileLog::begin();
    }
#endif
    if (startFailed || g_fsUploadError[0]) {
        ESP32BASE_LOG_W("web", "fs_upload_rejected path=%s error=%s", g_fsUploadPath[0] ? g_fsUploadPath : "-", g_fsUploadError[0] ? g_fsUploadError : "upload rejected");
        fsSendUploadJson(400, false, g_fsUploadError[0] ? g_fsUploadError : "upload rejected", g_fsUploadPath);
        return;
    }
    const int64_t actualSize = g_fsUploadPath[0] ? Esp32BaseFs::fileSize(g_fsUploadPath) : -1;
    if (actualSize >= 0 &&
        static_cast<uint64_t>(actualSize) == g_fsUploadBytes &&
        fsUploadFileReadableEnd(g_fsUploadPath, static_cast<uint64_t>(actualSize))) {
        ESP32BASE_LOG_W("web", "fs_upload_completed path=%s bytes=%lu overwrite=%s",
                        g_fsUploadPath,
                        static_cast<unsigned long>(g_fsUploadBytes),
                        g_fsUploadOverwrite ? "yes" : "no");
        if (beginResponse(200, "application/json", nullptr)) {
            sendChunk("{\"ok\":true,\"redirect\":\"/esp32base/fs?manage=1&uploaded=1\",\"path\":\"");
            sendEscapedJsonChunk(g_fsUploadPath);
            sendChunk("\"}");
            endResponse();
        }
        return;
    }
    fsSendUploadJson(500, false, "Upload verification failed", g_fsUploadPath);
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
        g_fsUploadForbidden = false;
        g_fsUploadStartFailed = false;
        g_fsUploadActive = false;
        g_fsUploadOverwrite = g_server.hasArg("overwrite") && g_server.arg("overwrite") == "1";
        g_fsUploadBytes = 0;
        g_fsUploadPath[0] = '\0';
        g_fsUploadError[0] = '\0';
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
        strlcpy(dir, g_server.hasArg("dir") ? g_server.arg("dir").c_str() : "", sizeof(dir));
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
        if (fsUploadTargetIsDirectory(g_fsUploadPath)) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("Target is a directory");
            return;
        }
#if ESP32BASE_ENABLE_FILELOG
        if (fileLogOwnsPath(g_fsUploadPath) && Esp32BaseFileLog::isEnabled()) {
            Esp32BaseFileLog::flush();
        }
#endif
        const bool exists = Esp32BaseFs::fileSize(g_fsUploadPath) >= 0;
        if (exists && !g_fsUploadOverwrite) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("File exists");
            return;
        }
        g_fsUploadFile = LittleFS.open(g_fsUploadPath, "w");
        if (!g_fsUploadFile) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("Could not open target file");
            return;
        }
        g_fsUploadActive = true;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (g_fsUploadForbidden || g_fsUploadStartFailed || !g_fsUploadActive) {
            return;
        }
        const size_t written = g_fsUploadFile.write(upload.buf, upload.currentSize);
        g_fsUploadBytes += written;
        if (written != upload.currentSize) {
            g_fsUploadStartFailed = true;
            fsSetUploadError("File write failed");
            fsCloseUploadFile();
        }
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
    strlcpy(path, g_server.hasArg("path") ? g_server.arg("path").c_str() : "", sizeof(path));
    if (!validFsDeletePath(path)) {
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
#if ESP32BASE_ENABLE_FILELOG
    const bool targetIsFileLog = fileLogOwnsPath(path);
    const bool retryFileLogAfterDelete = Esp32BaseFileLog::faulted();
    if (targetIsFileLog && Esp32BaseFileLog::isEnabled()) {
        ESP32BASE_LOG_W("web", "fs_delete_requested path=%s target=filelog", path);
        Esp32BaseFileLog::flush();
    }
#endif
    const bool ok = Esp32BaseFs::removeFile(path);
#if ESP32BASE_ENABLE_FILELOG
    if (!targetIsFileLog) {
        ESP32BASE_LOG_W("web", "fs_delete_requested path=%s result=%s", path, ok ? "success" : "failed");
    }
    if (ok && (targetIsFileLog || retryFileLogAfterDelete)) {
        Esp32BaseFileLog::begin();
    }
#else
    ESP32BASE_LOG_W("web", "fs_delete_requested path=%s result=%s", path, ok ? "success" : "failed");
#endif
    redirectSeeOther(ok ? "/esp32base/fs?manage=1&deleted=1" : "/esp32base/fs?manage=1&error=delete_failed");
}
#endif

} // namespace esp32base_web

#endif
