#pragma once

#include <stddef.h>
#include <stdint.h>

namespace esp32base_web {

enum class StreamBodyKind : uint8_t {
    Generic,
    OtaRaw,
    OtaMultipart,
    FsMultipart
};

enum class StreamBodyPreflightResult : uint8_t {
    Allowed,
    LengthRequired,
    TooLarge,
    SizeMismatch
};

inline StreamBodyPreflightResult evaluateStreamBodyLength(
    StreamBodyKind kind,
    size_t contentLength,
    size_t declaredPayloadBytes,
    size_t availablePayloadBytes,
    size_t genericLimitBytes,
    size_t uploadOverheadBytes) {
    if (contentLength == 0) {
        return StreamBodyPreflightResult::LengthRequired;
    }
    if (contentLength > genericLimitBytes) {
        return StreamBodyPreflightResult::TooLarge;
    }

    switch (kind) {
        case StreamBodyKind::OtaRaw:
            if (declaredPayloadBytes == 0 ||
                declaredPayloadBytes > availablePayloadBytes) {
                return StreamBodyPreflightResult::TooLarge;
            }
            if (contentLength < declaredPayloadBytes) {
                return StreamBodyPreflightResult::SizeMismatch;
            }
            return contentLength - declaredPayloadBytes <= uploadOverheadBytes
                       ? StreamBodyPreflightResult::Allowed
                       : StreamBodyPreflightResult::TooLarge;

        case StreamBodyKind::OtaMultipart:
            if (declaredPayloadBytes == 0 ||
                declaredPayloadBytes > availablePayloadBytes) {
                return StreamBodyPreflightResult::TooLarge;
            }
            if (contentLength < declaredPayloadBytes) {
                return StreamBodyPreflightResult::SizeMismatch;
            }
            return contentLength - declaredPayloadBytes <= uploadOverheadBytes
                       ? StreamBodyPreflightResult::Allowed
                       : StreamBodyPreflightResult::TooLarge;

        case StreamBodyKind::FsMultipart:
            if (contentLength <= uploadOverheadBytes) {
                return StreamBodyPreflightResult::Allowed;
            }
            return contentLength - uploadOverheadBytes <= availablePayloadBytes
                       ? StreamBodyPreflightResult::Allowed
                       : StreamBodyPreflightResult::TooLarge;

        case StreamBodyKind::Generic:
        default:
            return StreamBodyPreflightResult::Allowed;
    }
}

} // namespace esp32base_web
