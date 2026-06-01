#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

struct Esp32BaseAppEventRecord {
    uint32_t magic;
    uint32_t id;
    uint32_t epochSec;
    uint32_t bootId;
    uint32_t uptimeSec;
    int32_t value1;
    int32_t value2;
    int32_t value3;
    uint16_t code;
    uint8_t level;
    uint8_t flags;
    uint8_t valueMask;
    uint8_t reserved;
    uint16_t crc16;
    char source[12];
    char type[24];
    char reason[24];
    char object[56];
    char text[32];
};

static_assert(sizeof(Esp32BaseAppEventRecord) == 188, "Esp32BaseAppEventRecord layout changed");

class Esp32BaseAppEventLog {
public:
    enum Level : uint8_t {
        LEVEL_INFO = 1,
        LEVEL_WARN = 2,
        LEVEL_ERROR = 3
    };

    enum ValueMask : uint8_t {
        VALUE1 = 1 << 0,
        VALUE2 = 1 << 1,
        VALUE3 = 1 << 2
    };

    enum Flags : uint8_t {
        FLAG_TIME_SYNCED = 1 << 0,
        FLAG_TEXT_TRUNCATED = 1 << 1
    };

    enum StoreRecordStatus : uint8_t {
        STORE_RECORD_OK = 0,
        STORE_RECORD_EMPTY = 1,
        STORE_RECORD_INVALID_MAGIC = 2,
        STORE_RECORD_INVALID_LEVEL = 3,
        STORE_RECORD_CRC_MISMATCH = 4,
        STORE_RECORD_UNCOMMITTED = 5,
        STORE_RECORD_READ_FAILED = 6
    };

    struct Event {
        Level level = LEVEL_INFO;
        const char* source = nullptr;
        const char* type = nullptr;
        const char* reason = nullptr;
        const char* object = nullptr;
        uint16_t code = 0;
        int32_t value1 = 0;
        int32_t value2 = 0;
        int32_t value3 = 0;
        uint8_t valueMask = 0;
        const char* text = nullptr;
    };

    struct TimeSnapshot {
        bool synced;
        uint32_t epochSec;
        uint32_t bootId;
        uint32_t uptimeSec;
    };

    struct StoreInfo {
        const char* path = nullptr;
        int64_t fileSize = -1;
        uint32_t expectedFileSize = 0;
        uint16_t headerSize = 0;
        uint16_t recordSize = 0;
        uint16_t capacity = 0;
        uint16_t count = 0;
        uint16_t validCount = 0;
        uint16_t head = 0;
        uint32_t nextId = 0;
        uint32_t sequence = 0;
        uint8_t activeHeader = 0;
        bool ready = false;
        bool faulted = false;
    };

    struct StoreRecord {
        Esp32BaseAppEventRecord record;
        StoreRecordStatus status = STORE_RECORD_READ_FAILED;
        uint16_t slot = 0;
        uint16_t index = 0;
        uint32_t offset = 0;
        uint16_t storedCrc16 = 0;
        uint16_t calculatedCrc16 = 0;
        bool readOk = false;
        bool magicOk = false;
        bool levelOk = false;
        bool crcOk = false;
        bool committed = false;
    };

    using TimeProvider = TimeSnapshot (*)();
    using ReadCallback = void (*)(const Esp32BaseAppEventRecord& event, void* user);
    using StoreRecordCallback = void (*)(const StoreRecord& item, void* user);

    static bool begin();
    static bool reload();
    static bool append(const Event& event);
    static bool readLatest(uint16_t offset, uint16_t limit, ReadCallback cb, void* user = nullptr);
    static bool readStoreInfo(StoreInfo& info);
    static bool readStoreRecords(uint16_t offset, uint16_t limit, StoreRecordCallback cb, void* user = nullptr);
    static bool clear();
    static void setTimeProvider(TimeProvider provider);
    static bool isReady();
    static bool faulted();
    static const char* lastError();
    static uint16_t count();
    static uint16_t capacity();
    static uint32_t nextId();
    static const char* path();
    static const char* levelName(Level level);
    static const char* storeRecordStatusName(StoreRecordStatus status);
};
