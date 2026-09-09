#pragma once

namespace esp32base_internal {
// Owned by the facade; called on the system task after upload validation and
// before Update.begin. False vetoes upload and releases prepared resources.
// No platform protocol or MQTT dependency in Update.
using PreOtaUploadHook = bool (*)();
void registerPreOtaUploadHook(PreOtaUploadHook hook);
}
