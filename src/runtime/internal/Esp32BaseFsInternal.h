#pragma once

#include <stddef.h>
#include <stdint.h>

namespace esp32base_internal {

struct FsWriteSegment {
    const uint8_t* data;
    size_t length;
};

using FsReadVisitor = bool (*)(const uint8_t* data, size_t length, void* user);

using FsFixedSlotVisitor = bool (*)(uint32_t slotIndex,
                                    uint32_t slotByteOffset,
                                    const uint8_t* data,
                                    size_t length,
                                    void* user);

// All LittleFS calls are serialized by the implementation. Maintenance keeps
// the recursive filesystem lock across a multi-step format/remount/reload
// sequence. Write suspension is used by OTA and rejects new filesystem
// mutations after any in-flight operation has completed.
bool fsBeginExclusiveMaintenance();
void fsEndExclusiveMaintenance();
bool fsSetWritesSuspended(bool suspended);
bool fsWritesSuspended();
bool fsCurrentTaskInOperation();

bool fsWriteSegmentsAt(const char* path,
                       uint32_t offset,
                       const FsWriteSegment* segments,
                       size_t segmentCount);

bool fsCreateWithSegments(const char* path,
                          const FsWriteSegment* segments,
                          size_t segmentCount);

bool fsAppendSegments(const char* path,
                      const FsWriteSegment* segments,
                      size_t segmentCount);

bool fsVisitBytesAt(const char* path,
                    uint32_t offset,
                    uint32_t length,
                    FsReadVisitor visitor,
                    void* user);

bool fsVisitFixedSlots(const char* path,
                       uint32_t dataOffset,
                       uint32_t slotSize,
                       uint32_t capacity,
                       uint32_t firstSlot,
                       uint32_t slotCount,
                       bool descending,
                       FsFixedSlotVisitor visitor,
                       void* user);

} // namespace esp32base_internal
