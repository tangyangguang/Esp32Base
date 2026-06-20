#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class Esp32BaseTime {
public:
    enum Source : uint8_t {
        SOURCE_UPTIME = 0,
        SOURCE_RTC = 1,
        SOURCE_NTP = 2
    };

    struct Snapshot {
        bool synced;
        Source source;
        uint32_t epochSec;
        uint32_t uptimeSec;
        uint32_t bootId;
        uint32_t bootStartEpochSec;
    };

    typedef void (*TimeSyncCallback)(const Snapshot& snapshot);

    static bool initBootSession();
    static Snapshot snapshot();
    static bool isRealTime();
    static bool formatTime(char* out, size_t len, const char* fmt = nullptr);
    static bool resolveCurrentBootEvent(uint32_t bootId, uint32_t uptimeSec, uint32_t* epochSec);
    static void onTimeSynced(TimeSyncCallback callback);
    static const char* sourceName(Source source);
    static const char* logTimeString();
};
