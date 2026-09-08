#include <Arduino.h>
#include <Esp32Base.h>

namespace {

Esp32BaseRecordStore g_doorOpeningRecords;
bool g_consumerReady = false;
uint32_t g_nextConsumeId = 1;
uint8_t g_consumedSinceCheckpoint = 0;

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
    // This demo's consumer is the local Serial sink, not a remote platform ACK.
    // Ordinary recent history can instead keep the default RotateOldest policy.
    definition.retentionPolicy = Esp32BaseRecordStore::RetentionPolicy::PreserveUnreleased;
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
    Esp32BaseRecordStore::StoreStatus status;
    g_doorOpeningRecords.readStatus(status);
    g_nextConsumeId = status.releasedThroughRecordId + 1U;
    g_consumerReady = status.releasedThroughRecordId != UINT32_MAX;
}

void loop() {
    Esp32Base::handle();
    if (g_consumerReady) {
        Esp32BaseRecordStore::StoreStatus status;
        g_doorOpeningRecords.readStatus(status);
        if (g_nextConsumeId < status.nextRecordId || status.nextRecordId == 0) {
            uint8_t payload[8];
            Esp32BaseRecordStore::RecordMetadata metadata;
            const auto result = g_doorOpeningRecords.readById(
                g_nextConsumeId, payload, sizeof(payload), metadata);
            if (result != Esp32BaseRecordStore::RecordReadResult::Found) {
                // Do not skip a missing/corrupt fact and release a later ID.
                ESP32BASE_LOG_E("example", "consumer_stopped id=%lu result=%u",
                                (unsigned long)g_nextConsumeId, (unsigned)result);
                g_consumerReady = false;
            } else {
                Esp32BaseRecordStore::RecordView view;
                view.recordId = metadata.recordId;
                view.timing = metadata.timing;
                view.payload = payload;
                view.payloadSizeBytes = sizeof(payload);
                printDoorOpening(view, nullptr); // Local demonstration consumption only.
                if (!g_doorOpeningRecords.releaseThrough(g_nextConsumeId)) {
                    g_consumerReady = false;
                } else {
                    g_consumerReady = g_nextConsumeId != UINT32_MAX;
                    if (g_consumerReady) ++g_nextConsumeId;
                    if (++g_consumedSinceCheckpoint >= 32) {
                        if (!g_doorOpeningRecords.checkpointRelease()) {
                            ESP32BASE_LOG_E("example", "checkpoint_failed error=%s",
                                            g_doorOpeningRecords.lastErrorReason());
                            g_consumerReady = false;
                        } else {
                            g_consumedSinceCheckpoint = 0;
                        }
                    }
                }
            }
        }
    }
    delay(10);
}
