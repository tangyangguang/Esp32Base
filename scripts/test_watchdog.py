#!/usr/bin/env python3
"""Run the actual watchdog implementation against a fake SDK for both Core APIs."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADERS = {
    'Arduino.h': '#pragma once\n#include <stdint.h>\n#include <stddef.h>\n',
    'sdkconfig.h': '#pragma once\n#define CONFIG_ESP_TASK_WDT_TIMEOUT_S 5\n#define CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 1\n',
    'freertos/task.h': '''#pragma once
using TaskHandle_t = void*;
extern TaskHandle_t currentTask;
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return currentTask; }
''',
    'esp_system.h': '''#pragma once
enum esp_reset_reason_t { ESP_RST_POWERON, ESP_RST_TASK_WDT, ESP_RST_INT_WDT, ESP_RST_WDT };
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_TASK_WDT; }
''',
    'esp_task_wdt.h': '''#pragma once
#include <stdint.h>
#include <freertos/task.h>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_ERR_INVALID_STATE=1, ESP_ERR_NOT_FOUND=2, ESP_FAIL=3;
struct esp_task_wdt_config_t { uint32_t timeout_ms; uint32_t idle_core_mask; bool trigger_panic; };
esp_err_t esp_task_wdt_status(TaskHandle_t);
esp_err_t esp_task_wdt_add(TaskHandle_t);
esp_err_t esp_task_wdt_reset();
#if ESP_ARDUINO_VERSION_MAJOR >= 3
esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t*);
#else
esp_err_t esp_task_wdt_init(uint32_t, bool);
#endif
''',
}
MAIN = r'''
#include <cassert>
#include <cstdio>
#include "esp_task_wdt.h"
#include "src/runtime/Esp32BaseWatchdog.inc"
TaskHandle_t currentTask = reinterpret_cast<void*>(1);
bool initialized, subscribed;
int initCalls, addCalls, feeds, writes, initError, addError, statusError;
int32_t Esp32BaseConfig::getInt(const char*, const char*, int32_t) { return 7; }
bool Esp32BaseConfig::setInt(const char*, const char*, int32_t n) { assert(n==8); ++writes; return true; }
void Esp32BaseLog::write(Level, const char*, const char*, ...) {}
esp_err_t esp_task_wdt_status(TaskHandle_t) {
    if (statusError) return statusError;
    return !initialized ? ESP_ERR_INVALID_STATE : subscribed ? ESP_OK : ESP_ERR_NOT_FOUND;
}
esp_err_t esp_task_wdt_add(TaskHandle_t) {
    ++addCalls; if (addError) return addError; subscribed=true; return ESP_OK;
}
esp_err_t esp_task_wdt_reset() { ++feeds; return ESP_OK; }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t* c) {
    assert(c->timeout_ms==5000 && c->idle_core_mask==1 && c->trigger_panic);
#else
esp_err_t esp_task_wdt_init(uint32_t seconds, bool panic) {
    assert(seconds==5 && panic);
#endif
    ++initCalls; if (initError) return initError; initialized=true; return ESP_OK;
}
void reset() {
    g_enabled=false; g_watchdogTask=nullptr; g_resetCountLoaded=false;
    g_longOperationTask=nullptr; g_longOperationDepth=0;
    currentTask=reinterpret_cast<void*>(1);
    initialized=false; subscribed=false;
    initCalls=addCalls=feeds=writes=initError=addError=statusError=0;
}
int main() {
    // Existing system protection is never initialized/reconfigured again.
    reset(); initialized=true;
    assert(Esp32BaseWatchdog::begin()); assert(initCalls==0 && addCalls==1);
    assert(Esp32BaseWatchdog::begin()); assert(addCalls==1 && writes==1);
    Esp32BaseWatchdog::feed(); assert(feeds==1);
    assert(Esp32BaseWatchdog::enterLongOperation());
    assert(Esp32BaseWatchdog::enterLongOperation());
    assert(Esp32BaseWatchdog::exitLongOperation());
    assert(Esp32BaseWatchdog::currentTaskInLongOperation());
    currentTask=reinterpret_cast<void*>(2);
    assert(!Esp32BaseWatchdog::begin());
    int before=feeds; Esp32BaseWatchdog::feed(); assert(feeds==before);
    assert(!Esp32BaseWatchdog::enterLongOperation());
    assert(!Esp32BaseWatchdog::exitLongOperation());
    currentTask=reinterpret_cast<void*>(1);
    assert(Esp32BaseWatchdog::exitLongOperation());
    assert(!Esp32BaseWatchdog::currentTaskInLongOperation());
    // A task already registered by Arduino/application is valid.
    reset(); initialized=subscribed=true;
    assert(Esp32BaseWatchdog::begin()); assert(initCalls==0 && addCalls==0);
    // Absent system uses the SDK timeout/idle configuration, panic enabled.
    reset(); assert(Esp32BaseWatchdog::begin()); assert(initCalls==1 && addCalls==1);
    // Failures are not reported as enabled; retry does not double-count resets.
    reset(); initError=ESP_FAIL;
    assert(!Esp32BaseWatchdog::begin()); assert(!Esp32BaseWatchdog::isEnabled());
    assert(addCalls==0); initError=0;
    assert(Esp32BaseWatchdog::begin()); assert(writes==1);
    reset(); initialized=true; addError=ESP_FAIL;
    assert(!Esp32BaseWatchdog::begin()); assert(!Esp32BaseWatchdog::isEnabled());
    addError=0; assert(Esp32BaseWatchdog::begin()); assert(writes==1 && initCalls==0);
    reset(); statusError=ESP_FAIL;
    assert(!Esp32BaseWatchdog::begin()); assert(initCalls==0 && addCalls==0);
    puts("watchdog lifecycle, ownership and failure paths passed");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix='esp32base-watchdog-') as directory:
        root = Path(directory)
        for name, content in HEADERS.items():
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(content)
        (root / 'test.cpp').write_text(MAIN)
        for major in (2, 3):
            binary = root / f'watchdog-core{major}'
            subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                            f'-DESP_ARDUINO_VERSION_MAJOR={major}', '-DESP32BASE_ENABLE_WATCHDOG=1',
                            '-I', str(root), '-I', str(ROOT), str(root / 'test.cpp'),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            print(f'Core {major} API passed', flush=True)


if __name__ == '__main__':
    main()
