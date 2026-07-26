#pragma once

#include <stdint.h>

extern uint32_t g_fakeRandom;
inline uint32_t esp_random() { return g_fakeRandom++; }
