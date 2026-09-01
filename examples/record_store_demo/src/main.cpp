#include <Arduino.h>
#include <Esp32Base.h>

namespace {

Esp32BaseRecordStore g_doorOpeningRecords;

void writeU16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value & 0xFFU);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

uint16_t readU16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

void encodeDoorOpening(uint8_t* payload,
                       uint16_t doorId,
                       uint16_t resultCode,
                       uint16_t peakCurrentMa,
                       uint8_t attemptCount,
                       uint8_t flags) {
    writeU16(payload, doorId);
    writeU16(payload + 2, resultCode);
    writeU16(payload + 4, peakCurrentMa);
    payload[6] = attemptCount;
    payload[7] = flags;
}

void printDoorOpening(const Esp32BaseRecordStore::RecordView& record, void*) {
    ESP32BASE_LOG_I("door_record",
                    "id=%lu boot=%lu completed_uptime=%lu duration=%lu door=%u result=%u peak_ma=%u attempts=%u flags=%u",
                    static_cast<unsigned long>(record.recordId),
                    static_cast<unsigned long>(record.timing.completedBootId),
                    static_cast<unsigned long>(record.timing.completedUptimeSec),
                    static_cast<unsigned long>(record.timing.durationSec),
                    static_cast<unsigned>(readU16(record.payload)),
                    static_cast<unsigned>(readU16(record.payload + 2)),
                    static_cast<unsigned>(readU16(record.payload + 4)),
                    static_cast<unsigned>(record.payload[6]),
                    static_cast<unsigned>(record.payload[7]));
}

} // namespace

void setup() {
    Esp32Base::setFirmwareInfo("record-store-demo", "1.0.0");
    Esp32Base::begin();

    Esp32BaseRecordStore::StoreDefinition definition;
    definition.recordTypeName = "door-opening";
    definition.storeVersion = 1;
    definition.payloadSizeBytes = 8;
    definition.maximumStoreBytes = 32UL * 1024UL;
    definition.minimumFileSystemFreeBytes = 8UL * 1024UL;
    if (!g_doorOpeningRecords.begin(definition)) {
        ESP32BASE_LOG_E("example", "record_store_begin_failed error=%s", g_doorOpeningRecords.lastErrorReason());
        return;
    }
    if (!Esp32BaseStorage::registerRecordStore(g_doorOpeningRecords)) {
        ESP32BASE_LOG_E("example", "record_store_registration_failed error=%s",
                        Esp32BaseStorage::lastErrorReason());
        return;
    }

    Esp32BaseRecordStore::RecordStartTime started;
    if (!g_doorOpeningRecords.captureStartTime(started)) {
        ESP32BASE_LOG_E("example", "record_start_capture_failed");
        return;
    }

    // Simulate one complete door-opening action. The payload contains only business fields;
    // RecordStore supplies ID, completion time, duration and CRC.
    delay(1000);
    uint8_t payload[8];
    encodeDoorOpening(payload, 1, 1, 820, 1, 0);
    if (!g_doorOpeningRecords.appendCompleted(started, payload, sizeof(payload))) {
        ESP32BASE_LOG_E("example", "record_append_failed error=%s", g_doorOpeningRecords.lastErrorReason());
        return;
    }

    uint8_t scratch[8];
    g_doorOpeningRecords.readLatest(0, 5, scratch, sizeof(scratch), printDoorOpening);
}

void loop() {
    Esp32Base::handle();
    delay(10);
}
