#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace esp32base_web {

// One budget covers all chunks of a response. The SDK's individual write call
// may block until its own timeout; this check cannot preempt that call.
template <typename Client, typename Feed>
bool writeResponseBytes(Client& client, const char* data, size_t len,
                        uint32_t startedMs, uint32_t timeoutMs, Feed feed) {
    size_t offset = 0;
    uint8_t zeroProgress = 0;
    while (offset < len) {
        if (millis() - startedMs >= timeoutMs || !client.connected()) {
            return false;
        }
        feed();
        const size_t written = client.write(
            reinterpret_cast<const uint8_t*>(data + offset), len - offset);
        feed();
        if (millis() - startedMs >= timeoutMs || written > len - offset) {
            return false;
        }
        if (written == 0) {
            if (!client.connected() || ++zeroProgress >= 3) {
                return false;
            }
            delay(1);
            continue;
        }
        offset += written;
        zeroProgress = 0;
        yield();
    }
    return true;
}

}
