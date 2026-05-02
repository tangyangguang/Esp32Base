#include "Esp32Base.h"

#if !defined(ESP32)
#error "Esp32Base supports ESP32 Arduino Core targets only."
#endif

#include "Esp32BaseLog.h"

#include <string.h>

bool Esp32Base::_ready = false;
bool Esp32Base::_startupLogged = false;
const char* Esp32Base::_lastError = "";
char Esp32Base::_hostname[ESP32BASE_HOSTNAME_LEN] = "esp32base";
char Esp32Base::_firmwareName[ESP32BASE_FIRMWARE_NAME_LEN] = "";
char Esp32Base::_firmwareVersion[ESP32BASE_FIRMWARE_VERSION_LEN] = "";
char Esp32Base::_firmwareBuild[ESP32BASE_FIRMWARE_BUILD_LEN] = "";

bool Esp32Base::begin() {
    if (_ready) {
        return true;
    }

    Esp32BaseLog::begin();
    ESP32BASE_LOG_I("Esp32Base", "begin");

    if (!Esp32BaseConfig::begin()) {
        return fail("config begin failed");
    }

    if (!Esp32BaseRuntime::begin()) {
        return fail("runtime begin failed");
    }

    if (!Esp32BaseNetwork::begin()) {
        return fail("network begin failed");
    }

    if (!Esp32BaseWeb::begin()) {
        return fail("web begin failed");
    }

    _ready = true;
    _lastError = "";
    _startupLogged = false;

    ESP32BASE_LOG_I("Esp32Base", "ready=1 hostname=%s", _hostname);
    return true;
}

void Esp32Base::handle() {
    if (!_ready) {
        return;
    }

    Esp32BaseConfig::handle();
    Esp32BaseNetwork::handle();
    Esp32BaseWeb::handle();
    Esp32BaseRuntime::handle();

    if (!_startupLogged) {
        _startupLogged = true;
        logStartupConfig();
        logResources();
    }
}

void Esp32Base::setFirmwareInfo(const char* name, const char* version, const char* build) {
    copyText(name, _firmwareName, sizeof(_firmwareName));
    copyText(version, _firmwareVersion, sizeof(_firmwareVersion));
    copyText(build, _firmwareBuild, sizeof(_firmwareBuild));
}

void Esp32Base::setHostname(const char* hostname) {
    copyText(hostname, _hostname, sizeof(_hostname));
    if (_hostname[0] == '\0') {
        copyText("esp32base", _hostname, sizeof(_hostname));
    }
}

const char* Esp32Base::hostname() {
    return _hostname;
}

const char* Esp32Base::firmwareName() {
    return _firmwareName;
}

const char* Esp32Base::firmwareVersion() {
    return _firmwareVersion;
}

const char* Esp32Base::firmwareBuild() {
    return _firmwareBuild;
}

bool Esp32Base::isReady() {
    return _ready;
}

const char* Esp32Base::lastError() {
    return _lastError;
}

void Esp32Base::logStartupConfig() {
    ESP32BASE_LOG_I("Esp32Base", "fw name=%s version=%s build=%s hostname=%s",
                    _firmwareName[0] == '\0' ? "app" : _firmwareName,
                    _firmwareVersion,
                    _firmwareBuild,
                    _hostname);
    Esp32BaseConfig::logConfig();
    Esp32BaseNetwork::logConfig();
    Esp32BaseWeb::logConfig();
    Esp32BaseRuntime::logConfig();
}

void Esp32Base::logResources() {
    ESP32BASE_LOG_I("Esp32Base", "resources heap=%lu/%lu flash=%lu",
                    static_cast<unsigned long>(Esp32BaseRuntime::freeHeap()),
                    static_cast<unsigned long>(Esp32BaseRuntime::totalHeap()),
                    static_cast<unsigned long>(Esp32BaseRuntime::flashSize()));
}

void Esp32Base::copyText(const char* in, char* out, size_t len) {
    if (out == nullptr || len == 0U) {
        return;
    }

    const char* text = in == nullptr ? "" : in;
    strncpy(out, text, len - 1U);
    out[len - 1U] = '\0';
}

bool Esp32Base::fail(const char* message) {
    _ready = false;
    _lastError = message == nullptr ? "unknown error" : message;
    ESP32BASE_LOG_E("Esp32Base", "%s", _lastError);
    return false;
}
