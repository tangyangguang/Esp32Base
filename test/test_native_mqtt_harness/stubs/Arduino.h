#pragma once

#include <stddef.h>
#include <stdint.h>

extern uint32_t g_fakeMillis;
inline uint32_t millis() { return g_fakeMillis; }
inline void delay(uint32_t) {}

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
