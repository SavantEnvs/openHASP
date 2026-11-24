// Force-included (fuzz build only) into src/drv/tft/tft_driver_null.cpp by mayhem/build.sh.
// The null TFT driver starts a background thread that calls lv_tick_inc(5) every 5 ms forever.
// In a fuzz target that thread makes LVGL's timing (and so the executed code) depend on wall-clock
// time and races the fuzzing thread. Here its pthread_create is a no-op; the harnesses advance the
// LVGL tick themselves, deterministically (hasp_fuzz_common.h). Upstream source is unchanged.
#pragma once
#include <pthread.h>
static inline int hasp_fuzz_no_thread(void) { return 0; }
#define pthread_create(thread, attr, func, arg) ((void)(thread), (void)(attr), (void)(func), (void)(arg), hasp_fuzz_no_thread())
