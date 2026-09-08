#!/usr/bin/env python3
"""Run the production OTA module with fake Update/storage/time, without hardware."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADERS = {
'Arduino.h': '''#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
uint32_t millis();
void yield();
void delay(unsigned long);
''',
'esp_err.h': '''#pragma once
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_ERR_NOT_FOUND=-1;
inline const char* esp_err_to_name(int) { return "fake"; }
''',
'esp_log.h': '#pragma once\n',
'esp_timer.h': '#pragma once\n',
'esp_idf_version.h': '#pragma once\n#define ESP_IDF_VERSION_MAJOR 4\n',
'esp_ota_ops.h': '''#pragma once
#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>
enum esp_ota_img_states_t { ESP_OTA_IMG_NEW, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID, ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED, ESP_OTA_IMG_UNDEFINED };
constexpr int ESP_PARTITION_TYPE_APP=0, ESP_PARTITION_SUBTYPE_APP_OTA_MIN=16, ESP_PARTITION_SUBTYPE_APP_OTA_MAX=32;
struct esp_partition_t { char label[17]; uint32_t address; uint32_t size; int type; int subtype; };
extern esp_partition_t partition;
inline const esp_partition_t* esp_ota_get_running_partition() { return &partition; }
inline const esp_partition_t* esp_ota_get_boot_partition() { return &partition; }
inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return &partition; }
inline const esp_partition_t* esp_ota_get_last_invalid_partition() { return nullptr; }
inline int esp_ota_get_state_partition(const esp_partition_t*, esp_ota_img_states_t* s) { *s=ESP_OTA_IMG_VALID; return ESP_OK; }
inline int esp_ota_get_app_elf_sha256(char* p, size_t) { p[0]=0; return 0; }
inline bool esp_ota_check_rollback_is_possible() { return false; }
inline int esp_ota_mark_app_valid_cancel_rollback() { return ESP_OK; }
inline int esp_ota_mark_app_invalid_rollback_and_reboot() { return ESP_OK; }
''',
'Update.h': '''#pragma once
#include <stdint.h>
#include <stddef.h>
struct FakeUpdate {
    bool allowBegin=true, allowWrite=true, allowEnd=true, active=false;
    int starts=0, aborts=0;
    bool begin(size_t);
    size_t write(uint8_t*, size_t n) { return allowWrite ? n : 0; }
    bool end(bool) { active=false; return allowEnd; }
    void abort() { ++aborts; active=false; }
    const char* errorString() { return "fake Update failure"; }
};
extern FakeUpdate Update;
''',
'mbedtls/sha256.h': '''#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
struct mbedtls_sha256_context {};
inline void mbedtls_sha256_init(mbedtls_sha256_context*) {}
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
inline void mbedtls_sha256_starts(mbedtls_sha256_context*, int) {}
inline void mbedtls_sha256_update(mbedtls_sha256_context*, const uint8_t*, size_t) {}
inline void mbedtls_sha256_finish(mbedtls_sha256_context*, uint8_t* digest) { memset(digest, 0, 32); }
''',
}
MAIN = r'''
#include <cassert>
#include "src/update/Esp32BaseOta.inc"
uint32_t clockMs=0;
uint32_t millis() { return clockMs; }
void yield() {}
void delay(unsigned long n) { clockMs+=n; }
FakeUpdate Update;
esp_partition_t partition={"ota_1", 65536, 1024*1024, 0, 16};
bool paused=false, storagePaused=false, storageAvailable=true, fakePowerSave=true;
int preparations=0, watchdogDepth=0;
bool watchdogAllowed=true;
bool FakeUpdate::begin(size_t) {
    assert(preparations > 0 && paused && storagePaused && !fakePowerSave);
    ++starts; active=allowBegin; return allowBegin;
}
void Esp32BaseLog::write(Level, const char*, const char*, ...) {}
bool Esp32BaseFileLog::flush() { return true; }
bool Esp32BaseWatchdog::enterLongOperation() { if (!watchdogAllowed) return false; ++watchdogDepth; return true; }
bool Esp32BaseWatchdog::exitLongOperation() { assert(watchdogDepth > 0); --watchdogDepth; return true; }
void Esp32BaseWatchdog::feed() {}
void Esp32BaseConfig::pauseDeferredFlush() { paused=true; }
void Esp32BaseConfig::resumeDeferredFlush() { paused=false; }
bool Esp32BaseConfig::flushAll() { return true; }
bool Esp32BaseStorage::setOtaWriteSuspended(bool value) {
    if (value && !storageAvailable) return false;
    storagePaused=value; return true;
}
bool Esp32BaseWiFi::powerSave() { return fakePowerSave; }
void Esp32BaseWiFi::setPowerSave(bool value) { fakePowerSave=value; }
bool Esp32BaseSystem::appendRestartLog(const char*) { return true; }
void Esp32BaseSystem::restart(const char*) { assert(false); }
void prepare() { assert(!Update.active); ++preparations; }
void assertRestored() { assert(!paused && !storagePaused && fakePowerSave && !Update.active && watchdogDepth==0); }
void reset() {
    Update={}; preparations=0; clockMs=0; storageAvailable=true; watchdogAllowed=true;
    assertRestored(); g_otaStatus=Esp32BaseOta::READY;
    esp32base_internal::registerPreOtaUploadHook(prepare);
}
int main() {
    const uint8_t bytes[]={1,2,3,4};
    reset(); assert(!Esp32BaseOta::startUpload(0)); assert(preparations==0);
    reset(); assert(!Esp32BaseOta::startUpload(4, "invalid")); assert(preparations==0);
    reset(); storageAvailable=false;
    assert(!Esp32BaseOta::startUpload(4)); assert(preparations==0); assertRestored();
    reset(); watchdogAllowed=false;
    assert(!Esp32BaseOta::startUpload(4)); assert(preparations==0); assertRestored();
    reset(); Update.allowBegin=false;
    assert(!Esp32BaseOta::startUpload(4)); assert(preparations==1); assertRestored();
    reset(); assert(Esp32BaseOta::startUpload(4));
    assert(!Esp32BaseOta::startUpload(4)); assert(preparations==1);
    assert(Esp32BaseOta::writeChunk(bytes,4));
    assert(Esp32BaseOta::finishUpload()); assertRestored();
    reset(); assert(Esp32BaseOta::startUpload(4)); Update.allowWrite=false;
    assert(!Esp32BaseOta::writeChunk(bytes,4)); assertRestored();
    reset(); assert(Esp32BaseOta::startUpload(4));
    assert(!Esp32BaseOta::finishUpload()); assertRestored();
    reset(); assert(Esp32BaseOta::startUpload(4));
    assert(Esp32BaseOta::writeChunk(bytes,4)); Update.allowEnd=false;
    assert(!Esp32BaseOta::finishUpload()); assertRestored();
    reset(); assert(Esp32BaseOta::startUpload(4, "1111111111111111111111111111111111111111111111111111111111111111"));
    assert(Esp32BaseOta::writeChunk(bytes,4));
    assert(!Esp32BaseOta::finishUpload()); assertRestored();
    reset(); assert(Esp32BaseOta::startUpload(4));
    Esp32BaseOta::rejectUpload("rejected"); assertRestored();
    reset(); clockMs=UINT32_MAX-5000;
    assert(Esp32BaseOta::startUpload(4)); clockMs+=14999;
    Esp32BaseOta::handle(); assert(Esp32BaseOta::isUploading());
    clockMs+=1; Esp32BaseOta::handle(); assert(!Esp32BaseOta::isUploading()); assertRestored();
    reset(); assert(Esp32BaseOta::startUpload(4)); clockMs=14000;
    assert(Esp32BaseOta::writeChunk(bytes,2)); clockMs=16000;
    Esp32BaseOta::handle(); assert(Esp32BaseOta::isUploading());
    Esp32BaseOta::abortUpload("disconnect"); assertRestored();
}
'''
with tempfile.TemporaryDirectory(prefix='esp32base-ota-') as directory:
    root=Path(directory)
    for name,content in HEADERS.items():
        p=root/name; p.parent.mkdir(parents=True, exist_ok=True); p.write_text(content)
    (root/'test.cpp').write_text(MAIN)
    subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-I',str(root),'-I',str(ROOT),
                    '-DESP32BASE_ENABLE_OTA=1','-DESP32BASE_ENABLE_WEB=1','-DESP32BASE_ENABLE_WIFI=1',
                    '-DESP32BASE_ENABLE_WATCHDOG=1','-DESP32BASE_ENABLE_FS=1','-DESP32BASE_OTA_REQUIRE_MARK_VALID=0',
                    str(root/'test.cpp'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
    print('Production OTA: preparation ordering, success, rejection/failure cleanup and stalled upload passed')

# Exercise the boot guard and the cooperative fallback together, using the
# production module. Timer callbacks are driven explicitly at the deadline.
guard_headers = dict(HEADERS)
guard_headers['esp_log.h'] = '#pragma once\n#define ESP_EARLY_LOGE(...) ((void)0)\n'
guard_headers['esp_timer.h'] = '''#pragma once
#include <stdint.h>
using esp_timer_handle_t = void*;
struct esp_timer_create_args_t { void (*callback)(void*); const char* name; };
extern bool timerCreateAllowed, timerStartAllowed;
extern uint64_t timerDelayUs;
extern void (*timerCallback)(void*);
inline int esp_timer_create(const esp_timer_create_args_t* a, esp_timer_handle_t* h) {
    if (!timerCreateAllowed) return -2;
    timerCallback=a->callback; *h=reinterpret_cast<void*>(1); return 0;
}
inline int esp_timer_start_once(esp_timer_handle_t, uint64_t us) {
    timerDelayUs=us; return timerStartAllowed ? 0 : -2;
}
inline int esp_timer_stop(esp_timer_handle_t) { return 0; }
'''
guard_headers['esp_ota_ops.h'] = guard_headers['esp_ota_ops.h'].replace(
    'extern esp_partition_t partition;',
    'extern esp_partition_t partition;\nextern esp_ota_img_states_t fakeOtaState;\nextern int rollbackCalls;').replace(
    '*s=ESP_OTA_IMG_VALID;', '*s=fakeOtaState;').replace(
    'esp_ota_mark_app_valid_cancel_rollback() { return ESP_OK; }',
    'esp_ota_mark_app_valid_cancel_rollback() { fakeOtaState=ESP_OTA_IMG_VALID; return ESP_OK; }').replace(
    'esp_ota_mark_app_invalid_rollback_and_reboot() { return ESP_OK; }',
    'esp_ota_mark_app_invalid_rollback_and_reboot() { ++rollbackCalls; return ESP_OK; }')
guard_main = MAIN[:MAIN.index('int main()')] + r'''
bool timerCreateAllowed=true, timerStartAllowed=true;
uint64_t timerDelayUs=0;
void (*timerCallback)(void*)=nullptr;
esp_ota_img_states_t fakeOtaState=ESP_OTA_IMG_PENDING_VERIFY;
int rollbackCalls=0;
void resetGuard() {
    clockMs=0; rollbackCalls=0; fakeOtaState=ESP_OTA_IMG_PENDING_VERIFY;
    timerCreateAllowed=timerStartAllowed=true; timerDelayUs=0; timerCallback=nullptr;
    g_otaReady=true; g_otaBootPartition=&partition; g_otaBootMs=0;
    g_otaBootTimingStarted=true; g_otaMarkValidTimeoutHandled=false;
    g_otaBootGuardTimer=nullptr; g_otaBootGuardArmed=false; g_otaBootGuardArmError=ESP_OK;
}
int main() {
    resetGuard(); armOtaBootGuard(); assert(timerDelayUs==30000000);
    clockMs=30000; Esp32BaseOta::handle(); assert(rollbackCalls==0);
    timerCallback(nullptr); assert(rollbackCalls==1);
    Esp32BaseOta::handle(); assert(rollbackCalls==1);
    resetGuard(); timerCreateAllowed=false; armOtaBootGuard();
    clockMs=30000; Esp32BaseOta::handle(); Esp32BaseOta::handle(); assert(rollbackCalls==1);
    resetGuard(); timerStartAllowed=false; armOtaBootGuard();
    clockMs=30000; Esp32BaseOta::handle(); Esp32BaseOta::handle(); assert(rollbackCalls==1);
    resetGuard(); armOtaBootGuard(); clockMs=29999;
    assert(Esp32BaseOta::markCurrentValid()); timerCallback(nullptr);
    clockMs=30000; Esp32BaseOta::handle(); assert(rollbackCalls==0);
    resetGuard(); timerCreateAllowed=false; armOtaBootGuard();
    clockMs=20000; timerCreateAllowed=true; armOtaBootGuard(); assert(timerDelayUs==10000000);
    resetGuard(); g_otaBootMs=UINT32_MAX-9999; clockMs=10000;
    armOtaBootGuard(); assert(timerDelayUs==10000000);
    resetGuard(); clockMs=30001; armOtaBootGuard(); assert(timerDelayUs==1);
}
'''
with tempfile.TemporaryDirectory(prefix='esp32base-ota-guard-') as directory:
    root=Path(directory)
    for name,content in guard_headers.items():
        p=root/name; p.parent.mkdir(parents=True, exist_ok=True); p.write_text(content)
    (root/'test.cpp').write_text(guard_main)
    subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-I',str(root),'-I',str(ROOT),
                    '-DESP32BASE_ENABLE_OTA=1','-DESP32BASE_ENABLE_WEB=1','-DESP32BASE_ENABLE_WIFI=1',
                    '-DESP32BASE_ENABLE_WATCHDOG=1','-DESP32BASE_ENABLE_FS=1','-DESP32BASE_OTA_REQUIRE_MARK_VALID=1',
                    str(root/'test.cpp'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
    print('Production OTA boot guard: one timeout owner, failed timer fallback, confirmation and original deadline passed')
