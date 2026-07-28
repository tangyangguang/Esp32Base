#pragma once

#include <cstddef>
#include <cstdint>

enum SeekMode {
    SeekSet,
    SeekCur,
    SeekEnd
};

inline void yield() {}
