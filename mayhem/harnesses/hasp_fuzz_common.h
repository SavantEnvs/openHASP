// Shared one-time init + per-input reset for the openHASP fuzz harnesses.
// Mirrors the HASP_TARGET_PC branch of setup() in src/main.cpp (minus MQTT / network).
#pragma once
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hasplib.h"
#include "hasp_oobe.h"
#include "hasp_gui.h"
#include "hasp_config.h"
#include "hasp_debug.h"
#include "dev/device.h"
#include "lv_fs_if.h"
#include "mqtt/hasp_mqtt.h"

// Process-global state the dispatch commands can change (hasp_dispatch.cpp / hasp.cpp); snapshotted
// after init and restored after every input by hasp_fuzz_reset().
extern uint8_t nCommands;
extern haspCommand_t commands[];
extern moodlight_t moodlight;
extern uint16_t haspThemeHue;
extern uint8_t haspThemeId;

// The `jsonl` command handler (registered via dispatch_add_command in dispatchSetup); it has
// external linkage in hasp_dispatch.cpp but is not declared in hasp_dispatch.h.
void dispatch_parse_jsonl(const char*, const char* payload, uint8_t source);

// Environment mock: the PC build's `shell` command runs popen(payload) on a thread. Fuzzed
// input must never reach a real shell, so popen fails here (libc mock, upstream code untouched).
extern "C" FILE* popen(const char*, const char*)
{
    errno = EPERM;
    return NULL;
}

// The fuzz build has no tick thread (mayhem/harnesses/no_tick_thread.h): advance LVGL's clock by a
// fixed step before each task-handler pass, so timers, refresh and async deletes run deterministically.
static void hasp_fuzz_task_handler()
{
    lv_tick_inc(50);
    lv_task_handler();
}

// Defaults captured once after init: the module configs that `config/<module>` payloads and
// config.json can change, plus the device/theme state the backlight, moodlight, idle, theme and
// antiburn commands change.
struct hasp_fuzz_defaults_t
{
    JsonDocument config; // {"gui":{...},"hasp":{...},"mqtt":{...}}
    uint16_t theme_hue;
    moodlight_t moodlight;
    uint8_t backlight_level;
    bool backlight_power;
    uint8_t sleep_state;
    bool wakeup_touch;
};
static hasp_fuzz_defaults_t* hasp_fuzz_defaults = NULL;

static char hasp_fuzz_shot_path[512];

// `screenshot <filename>` fopen()s the payload as a path ("wb") on POSIX, so a fuzzed input could
// create or truncate any file the process can write. Keep the command's code path (bitmap header +
// full-frame flush through gui_screenshot_to_file) but aim it at one scratch file under $TMPDIR and
// delete it straight away, so no input leaves a file behind for a later `run`/`jsonl`/font load.
static void hasp_fuzz_screenshot(const char*, const char*, uint8_t)
{
    guiTakeScreenshot(hasp_fuzz_shot_path);
    unlink(hasp_fuzz_shot_path);
}

static void hasp_fuzz_snapshot_defaults()
{
    hasp_fuzz_defaults = new hasp_fuzz_defaults_t();
    JsonDocument& cfg  = hasp_fuzz_defaults->config;
    guiGetConfig(cfg[FPSTR(FP_GUI)].to<JsonObject>());
    haspGetConfig(cfg[FPSTR(FP_HASP)].to<JsonObject>());
    mqttGetConfig(cfg[FPSTR(FP_MQTT)].to<JsonObject>());
    hasp_fuzz_defaults->theme_hue       = haspThemeHue;
    hasp_fuzz_defaults->moodlight       = moodlight;
    hasp_fuzz_defaults->backlight_level = haspDevice.get_backlight_level();
    hasp_fuzz_defaults->backlight_power = haspDevice.get_backlight_power();
    hasp_fuzz_defaults->sleep_state     = hasp_get_sleep_state();
    lv_obj_t* layer                     = lv_disp_get_layer_sys(NULL);
    hasp_fuzz_defaults->wakeup_touch    = layer ? lv_obj_get_click(layer) : false;
}

// Put back everything hasp_fuzz_snapshot_defaults() captured.
static void hasp_fuzz_restore_defaults()
{
    hasp_fuzz_defaults_t* d = hasp_fuzz_defaults;
    hasp_set_antiburn(0, 0); // stops the antiburn task and restores the normal flush callback
    {
        JsonDocument cfg = d->config; // a fresh copy: the setters take the objects by reference
        haspThemeHue     = d->theme_hue;
        guiSetConfig(cfg[FPSTR(FP_GUI)].as<JsonObject>());
        haspSetConfig(cfg[FPSTR(FP_HASP)].as<JsonObject>());
        mqttSetConfig(cfg[FPSTR(FP_MQTT)].as<JsonObject>());
    }
    hasp_set_theme(haspThemeId);
    moodlight = d->moodlight;
    haspDevice.set_backlight_level(d->backlight_level);
    haspDevice.set_backlight_power(d->backlight_power);
    hasp_set_sleep_state(d->sleep_state);
    hasp_set_wakeup_touch(d->wakeup_touch);
}

static void hasp_fuzz_init()
{
    static bool done = false;
    if(done) return;
    done = true;

    // Files stay relative to the (writable) working directory Mayhem runs the target in:
    // lv_fs_pc resolves L:/ against the cwd, and config writes land there too. No chdir, so
    // libFuzzer's relative corpus/artifact paths keep working.
    // openHASP logs every command to stdout; libFuzzer reports on stderr.
    int devnull = open("/dev/null", O_WRONLY);
    if(devnull >= 0) dup2(devnull, 1);

    lv_init();
    lv_log_register_print_cb(debugLvglLogEvent);
    lv_fs_if_init();
    haspDevice.init();
    configSetup();
    dispatchSetup();
    guiSetup();
    if(!oobeSetup()) haspSetup();
    hasp_fuzz_task_handler();

    const char* tmp = getenv("TMPDIR");
    snprintf(hasp_fuzz_shot_path, sizeof(hasp_fuzz_shot_path), "%s/openhasp-fuzz-%d.bmp",
             (tmp && *tmp) ? tmp : "/tmp", (int)getpid());
    for(uint8_t i = 0; i < nCommands; i++)
        if(!strcasecmp(commands[i].p_cmdstr, "screenshot")) commands[i].func = hasp_fuzz_screenshot;
    hasp_fuzz_snapshot_defaults();
}

// Run LVGL's tasks (async deletes, animations, timers) without the full-screen software redraw
// (~20 ms per frame under ASan). Used by the command harnesses, whose target is the attribute and
// command code; fuzz_jsonl keeps rendering the objects it builds.
static void hasp_fuzz_tasks_no_render()
{
    lv_disp_t* disp = lv_disp_get_default();
    if(disp) disp->inv_p = 0;
    hasp_fuzz_task_handler();
}

// Restore the default config/device/theme state, drop every object on every page (page 0
// included) and go back to page 1, so each input starts from the same state and empty screen.
static void hasp_fuzz_reset()
{
    for(uint8_t pageid = 0; pageid <= HASP_NUM_PAGES; pageid++) haspPages.clear(pageid);
    hasp_fuzz_restore_defaults();
    haspPages.set(1, LV_SCR_LOAD_ANIM_NONE, 0, 0);
    hasp_fuzz_tasks_no_render();
}

// Copy into a NUL-terminated heap buffer (the dispatch API takes C strings).
static char* hasp_fuzz_cstr(const uint8_t* data, size_t size)
{
    char* buf = (char*)malloc(size + 1);
    memcpy(buf, data, size);
    buf[size] = 0;
    return buf;
}
