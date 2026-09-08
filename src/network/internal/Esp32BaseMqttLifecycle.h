#pragma once

namespace esp32base_internal {
// Stop accepting new publishes and request asynchronous transport disconnect.
// Does not dispatch application callbacks or claim that TLS has been released.
void suspendMqttForOta();
}
