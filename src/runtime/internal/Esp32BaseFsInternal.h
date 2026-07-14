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
