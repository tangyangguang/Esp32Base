#pragma once

#include "WebContext.h"

#if ESP32BASE_ENABLE_WEB

namespace esp32base_web {

#ifndef ESP32BASE_WEB_CSS_VERSION
#define ESP32BASE_WEB_CSS_VERSION __TIME__
#endif

extern const char WEB_CSS_TYPE[] PROGMEM;
extern const char WEB_CSS[] PROGMEM;
extern const char WEB_HEAD[] PROGMEM;
#if ESP32BASE_ENABLE_APP_CONFIG
extern const char WEB_APPCFG_STYLE[] PROGMEM;
#endif

const char* methodName(Esp32BaseWeb::Method method);
Esp32BaseWeb::Method fromHttpMethod(HTTPMethod method);
HTTPMethod toHttpMethod(Esp32BaseWeb::Method method);
const char* uiToneClass(Esp32BaseWeb::UiTone tone);
bool validFooterBarMode(Esp32BaseWeb::FooterBarMode mode);
const char* footerBarModeName(Esp32BaseWeb::FooterBarMode mode);
Esp32BaseWeb::FooterBarMode readFooterBarMode();
void flushChunkBuffer();
bool sendResponseContent(const char* data, size_t len);
bool sendRawChunkedContent(const char* data, size_t len);
bool sendResponseHeader(const char* name, const char* value);
bool beginResponse(int code, const char* contentType, const char* filename);
void endResponse();
void markRequest();
void sendChunk(const char* data, size_t len);
void sendChunk(const char* text);
void sendEscapedHtmlChunk(const char* text);
void sendEscapedJsonChunk(const char* text);
void sendIntChunk(int value);
void sendBytesJsonChunk(uint32_t bytes);
void formatReadableBytes(uint64_t bytes, char* out, size_t len);
bool ensureAuth();
uint8_t routeCount(bool appPageOnly);
bool routeMatchesMethod(const Route& route, Esp32BaseWeb::Method method);
Route* findRoute(const char* path, Esp32BaseWeb::Method method);
bool validAuthUser(const char* value);
bool validAuthPass(const char* value);
bool parseBasicAuth(char* user, size_t userLen, char* pass, size_t passLen);
bool authMatches(const char* user, const char* pass);
bool parseAndCheckAuth(const char* context);
void applyPlainAuth(const char* user, const char* pass);
void applyDefaultAuth();
void applyStoredAuth(const char* user, const char* pass);
bool loadStoredAuth();
bool saveStoredAuth(const char* user, const char* pass);
bool validHeaderValue(const char* value, size_t maxLen);
bool validHeaderName(const char* name, size_t maxLen);
bool sameHost(const char* a, const char* b);
bool extractUrlHost(const char* url, char* out, size_t len);
bool requestSameOrigin();
bool ensurePostAllowed(const char* context);
bool validDownloadFilename(const char* filename);
bool appendQuotedHeaderValue(char* out, size_t len, const char* value);
uint8_t appPageCount();
uint8_t navItemCount();
bool navPathExists(const char* path);
uint8_t appNavCount();
const char* configuredHomePath();
bool useAppHome();
bool isBuiltinWebPath(const char* path);
bool shouldSendHeadExtra();
bool navPathMatches(const char* navPath, const char* currentPath);
void updateActivePath(const char* candidate, const char* currentPath, const char*& activePath, size_t& activeLen);
const char* activeNavPath(bool includeSystemLinks);
void sendLink(const char* path, const char* title, bool paragraph, const char* extraClass);
void sendNavLink(const char* path, const char* title, bool paragraph, const char* activePath, const char* baseClass);
void sendPaginationLink(const char* label, const Esp32BaseWeb::Pagination& pagination, uint32_t page, bool disabled);
void sendPaginationPageNumber(const Esp32BaseWeb::Pagination& pagination, uint32_t page, uint32_t currentPage);
void sendPaginationActionPath(const char* path);
void sendHiddenInput(const char* name, const char* value);
void sendQueryHiddenInputs(const char* query);
void sendInfoRowStart(const char* label);
void sendInfoRowEnd();
void sendInfoRow(const char* label, const char* value);
void sendSubmetricsStart();
void sendSubmetric(const char* label, const char* value);
void sendSubmetricsEnd();
void sendStatusTag(Esp32BaseWeb::UiTone tone, const char* text);
void sendTaggedInfoRow(const char* label, const char* value, Esp32BaseWeb::UiTone tone);
void sendStatusSectionStart(const char* title);
void sendStatusSectionEnd();
#if ESP32BASE_ENABLE_FS
void fsScanYield();
void fsFormatCount(uint32_t count, char* out, size_t len);
bool fsJoinPath(const char* dir, const char* name, char* out, size_t len);
void fsAddTopFile(FsScan& scan, const char* path, uint64_t size);
void sendFsDeleteForm(const char* path);
void sendFsDownloadForm(const char* path);
void sendFsPathTooLongRow(const char* dir, const char* name, bool isDir);
void sendFsFileActions(const char* path, bool manage);
void sendFsUnreadableActions(const char* path, bool manage);
bool fsFileReadableStart(const char* path, size_t size);
void sendFsTreeRow(const char* path, size_t size, bool isDir, bool manage);
void fsWalkCallback(const char* name, size_t size, bool isDir, void* user);
void fsNoopListCallback(const char*, size_t, bool, void*);
bool fsWalkDir(FsScan& scan, const char* dir, uint8_t depth, bool emitRows, bool manage, uint16_t emitLimit, uint16_t* emittedRows);
bool scanFs(FsScan& scan);
bool fsManageMode();
bool fsInternalUsageHigh(const FsScan& scan);
bool validFsFilePath(const char* path);
bool fsReadArg(const char* name, char* out, size_t len);
bool fsReadPathArg(const char* name, char* out, size_t len);
bool validFsDeletePath(const char* path);
bool validFsDirectoryPath(const char* path);
bool fsUploadFilenameValid(const char* name);
bool fsUploadDirectoryExists(const char* dir);
bool fsBuildUploadPath(const char* dir, const char* name, char* out, size_t len);
void fsSetUploadError(const char* message);
void fsCloseUploadFile();
bool fsUploadTargetIsDirectory(const char* path);
bool fsUploadFileReadableEnd(const char* path, uint64_t size);
void fsSendJsonBool(const char* name, bool value, bool comma);
void fsSendUploadJson(int code, bool ok, const char* error, const char* path, bool exists, bool isDir);
void fsUploadDirOptionCallback(const char* name, size_t, bool isDir, void* user);
void sendFsUploadDirectoryOptionsFor(const char* dir, uint8_t depth, uint16_t* emitted, bool* pathTooLong);
void sendFsUploadPanel();
void fsDownloadFilename(const char* path, char* out, size_t len);
bool fileLogOwnsPath(const char* path);
void sendFsInventoryValue(const FsScan& scan, uint64_t fsUsed);
void sendFsSummaryCell(const char* label, const char* value);
void sendFsSummaryTable(const FsScan& scan);
void sendFsSummaryRows(const FsScan& scan);
#endif
#if ESP32BASE_ENABLE_WATCHDOG
WatchdogTripState readWatchdogTripState();
bool formatEpochTime(uint32_t epoch, char* out, size_t len);
void formatWatchdogTripResetAt(const WatchdogTripState& state, char* out, size_t len);
uint32_t currentWatchdogTripResetTime();
#endif
void formatMac(uint64_t mac, char* out, size_t len);
const char* partitionTypeName(esp_partition_type_t type);
const char* partitionSubtypeName(const esp_partition_t* partition);
bool samePartition(const esp_partition_t* a, const esp_partition_t* b);
void webBytesToHex(const uint8_t* bytes, size_t len, char* out, size_t outLen);
const char* otaImageStateName(const esp_partition_t* partition);
void partitionImageSha256(const esp_partition_t* partition, char* out, size_t len);
void partitionAppVersion(const esp_partition_t* partition, char* out, size_t len);
void sendPartitionJson(const char* key, const esp_partition_t* partition);
void sendAppPartitionsJson();
const char* partitionRole(const esp_partition_t* partition, const esp_partition_t* running, const esp_partition_t* boot, const esp_partition_t* nextOta);
void sendPartitionTable();
void sendAppLinks(bool paragraph, const char* activePath);
void sendSystemLinks(bool paragraph, const char* activePath);
void sendMainNav();
void sendSystemNavSection();
void sendFooterStats();
void sendProgmem(const char* p);
bool isAuthenticated();
void dispatchRoute(Route& route);
void registerRoute(Route& route);
bool responseClientConnected();
void feedWatchdogDuringSend();
bool writeClientBytes(WiFiClient& client, const char* data, size_t len);
void markResponseClientDisconnected();
void redirectSeeOther(const char* url);
void sendUintChunk(uint64_t value);
void handleStatus();
void handleChip();
void handleFirmware();
bool loadStoredHostname(char* out, size_t len);
bool hostnameRestartRequired(const char* storedHostname);
void sendHostnameJson(int code);
void handleHostnameApiGet();
void handleHostnameSubmit();
void handleWifiPage();
void handleWifiSubmit();
void handleWifiRetry();
void handleWifiClear();
#if ESP32BASE_ENABLE_FILELOG
void sendFileLogModeOption(const char* value, const char* label, Esp32BaseFileLog::Mode mode);
bool fileLogModeFromArg(const String& raw, Esp32BaseFileLog::Mode& mode);
const char* fileLogModeName(Esp32BaseFileLog::Mode mode);
const char* fileLogRuntimeStateName();
bool fileLogHasRuntimeDetails();
void sendFileLogRuntimeStateTag();
void sendFileLogRuntimeStateRow(const char* label);
void sendFileLogRuntimeNotice();
#endif
void sendFooterBarModeOption(const char* value, const char* label, Esp32BaseWeb::FooterBarMode mode);
bool footerBarModeFromArg(const String& raw, Esp32BaseWeb::FooterBarMode& mode);
#if ESP32BASE_ENABLE_WATCHDOG
void sendWatchdogPanel();
#endif
#if ESP32BASE_ENABLE_APP_CONFIG
bool validConfigName(const char* value);
bool appNsAllowed(const char* ns);
bool groupExists(const char* groupId);
bool fieldExists(const char* ns, const char* key);
bool validFieldCommon(const char* groupId, const char* ns, const char* key, const char* label);
bool validOptionalHelp(const char* help);
bool validOptionalUnit(const char* unit);
void appConfigLogRegisterFailed(const char* type, const char* ns, const char* key);
bool enumValueAllowed(const Esp32BaseAppConfig::EnumField& field, const char* value);
bool validEnumOptions(const Esp32BaseAppConfig::EnumField& field);
bool stepMatches(int32_t value, int32_t minValue, int32_t step);
bool parseStrictInt32(const char* text, int32_t& out);
int32_t pow10Int(uint8_t scale);
bool formatDecimalRaw(int32_t raw, uint8_t scale, char* out, size_t len);
bool parseDecimalRaw(const char* text, uint8_t scale, int32_t& rawOut);
void appConfigFieldName(uint8_t index, char* out, size_t len);
bool getSubmittedRaw(const AppConfigFieldSlot& field, uint8_t index, char* out, size_t len);
bool findSubmittedRaw(const char* ns, const char* key, char* out, size_t len, const AppConfigFieldSlot** fieldOut);
void readAppConfigValue(const AppConfigFieldSlot& field, char* textOut, size_t textLen, int32_t& rawOut, bool& boolOut);
const char* appConfigEnumLabel(const AppConfigFieldSlot& field, const char* value);
void appConfigDisplayText(const AppConfigFieldSlot& field, const char* text, bool boolean, char* out, size_t len);
void clearAppConfigPendingRestart(AppConfigPendingRestartSlot& slot);
AppConfigPendingRestartSlot* findAppConfigPendingRestart(uint8_t fieldIndex);
AppConfigPendingRestartSlot* allocAppConfigPendingRestart(uint8_t fieldIndex);
bool appConfigValueMatchesPendingOriginal(const AppConfigFieldSlot& field, const AppConfigPendingRestartSlot& slot,
                                          const char* text, int32_t raw, bool boolean);
void updateAppConfigPendingRestart(const AppConfigFieldSlot& field, uint8_t index,
                                   const char* oldText, int32_t oldRaw, bool oldBool,
                                   const char* newText, int32_t newRaw, bool newBool);
bool hasAppConfigPendingRestart();
void sendAppConfigScript();
void sendAppConfigTopMessage();
void sendAppConfigPendingRestartNotice();
void sendAppConfigPage(const char* errorMessage);
bool validateAllSubmitted(char* error, size_t errorLen);
bool validateSubmittedField(const AppConfigFieldSlot& field, const char* submitted, char* normalized, size_t normalizedLen,
                            char* error, size_t errorLen);
bool writeSubmittedField(const AppConfigFieldSlot& field, uint8_t index, Esp32BaseAppConfig::SaveSummary& summary);
void handleAppConfigSubmit();
void handleAppConfigPage();
#endif
void handleRestart();
void handleToolsPage();
void handleToolsFileLogPost();
void handleToolsFooterBarPost();
void handleToolsRebootPost();
void handleToolsWatchdogTripResetPost();
void handleToolsFormatFsPost();
void handleToolsLogsClearPost();
#if ESP32BASE_ENABLE_APP_EVENTS
void handleToolsAppEventsClearPost();
#endif
void handleCaptiveProbe();
void handleRootRedirect();
void handleNoContent();
void handleUiCss();
void handleNotFound();
#if ESP32BASE_ENABLE_OTA
bool parseSizeHeader(const String& value, size_t& out, char* error, size_t errorLen);
void handleOtaPage();
void handleOtaUploadDone();
void handleOtaUpload();
void handleOtaRawUpload();
void handleOtaApi();
#endif
void handleRoot();
#if ESP32BASE_ENABLE_FS
void handleFsPage();
void handleFsDownloadGet();
void handleFsCheckGet();
void handleFsUploadDone();
void handleFsUpload();
void handleFsDeletePost();
#endif
uint8_t selectedLogSegment();
void sendRawLogChunk(const char* data, size_t len, void* user);
void logSegmentTitle(uint8_t index, char* out, size_t len);
void sendLogSegmentTabs(uint8_t selected);
void handleLogsPage();
void handleLogsRaw();
void handleLogsClear();
#if ESP32BASE_ENABLE_APP_EVENTS
void handleAppEventsPage();
void handleAppEventsApi();
void handleAppEventsCsv();
#endif
void handleAuthPage();
void handleAuthSubmit();

} // namespace esp32base_web

#endif
