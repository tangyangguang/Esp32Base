#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "Esp32BaseRecordStore.h"

class Esp32BaseAppEvents {
public:
    enum class Level : uint16_t {
        Info = 1,
        Warning = 2,
        Error = 3
    };

    struct EventInput {
        uint32_t eventCode = 0;
        uint32_t reasonCode = 0;
        uint32_t objectId = 0;
        int32_t value1 = 0;
        int32_t value2 = 0;
        uint16_t flags = 0;
        Level level = Level::Info;
    };

    struct EventRecord {
        uint32_t recordId = 0;
        Esp32BaseRecordStore::RecordTiming timing;
        uint32_t eventCode = 0;
        uint32_t reasonCode = 0;
        uint32_t objectId = 0;
        int32_t value1 = 0;
        int32_t value2 = 0;
        uint16_t flags = 0;
        Level level = Level::Info;
    };

    struct EventStoreStatus {
        Esp32BaseRecordStore::StoreStatus storage;
    };

    enum class EventReadResult : uint8_t {
        Found,
        NotFound,
        Corrupt,
        IoError,
        InvalidArgument
    };

    using ReadCallback = void (*)(const EventRecord& event, void* user);

    static bool begin();
    static bool reload();
    static bool append(const EventInput& event);
    static bool readLatest(uint32_t offset, uint32_t limit, ReadCallback callback, void* user = nullptr);
    static EventReadResult readById(uint32_t recordId, EventRecord& event);
    static bool clear();
    static bool readStatus(EventStoreStatus& status);
    static bool isReady();
    static bool isWritable();
    static uint32_t count();
    static uint32_t capacity();
    static const char* path();
    static const char* lastErrorReason();
    static const char* levelName(Level level);
};
