#ifndef ESP32_BASE_CONFIG_H
#define ESP32_BASE_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#ifndef ESP32BASE_CONFIG_PENDING_MAX
#define ESP32BASE_CONFIG_PENDING_MAX 8
#endif

class Esp32BaseConfig {
public:
    static constexpr const char* RESERVED_NAMESPACE = "esp32base";

    static bool begin();
    static void handle();
    static bool isReady();

    static bool setStr(const char* ns, const char* key, const char* value);
    static bool getStr(const char* ns, const char* key, char* out, size_t len, const char* def = "");

    static bool setInt(const char* ns, const char* key, int32_t value);
    static int32_t getInt(const char* ns, const char* key, int32_t def = 0);

    static bool setBool(const char* ns, const char* key, bool value);
    static bool getBool(const char* ns, const char* key, bool def = false);

    static bool setIntDeferred(const char* ns, const char* key, int32_t value);
    static bool setBoolDeferred(const char* ns, const char* key, bool value);
    static bool flush();

    static uint8_t pendingCount();
    static uint8_t pendingCapacity();
    static void logConfig();

private:
    enum PendingType : uint8_t {
        PENDING_EMPTY = 0,
        PENDING_INT,
        PENDING_BOOL
    };

    struct PendingWrite {
        PendingType type;
        char ns[16];
        char key[16];
        int32_t value;
    };

    static bool validateName(const char* name);
    static bool writePending(const PendingWrite& item);
    static bool enqueue(PendingType type, const char* ns, const char* key, int32_t value);
    static void copyName(const char* in, char* out, size_t len);

    static bool _ready;
    static PendingWrite _pending[ESP32BASE_CONFIG_PENDING_MAX];
};

#endif
