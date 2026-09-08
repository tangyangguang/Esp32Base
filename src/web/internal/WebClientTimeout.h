#pragma once

#include <stdint.h>

namespace esp32base_web {

template <typename Client>
auto setWebClientTimeoutSeconds(Client& client, uint32_t seconds, int)
    -> decltype(client.setConnectionTimeout(seconds), void()) {
    // New NetworkClient separates socket timeout and Stream timeout, both in ms.
    client.setConnectionTimeout(seconds * 1000UL);
    client.setTimeout(seconds * 1000UL);
}

template <typename Client>
void setWebClientTimeoutSeconds(Client& client, uint32_t seconds, long) {
    // Older WiFiClient sets socket and Stream timeout together, in seconds.
    client.setTimeout(seconds);
}

} // namespace esp32base_web
