// config.json, the settings file openHASP loads at startup (and the `config/<module>` MQTT surface):
// configSetup() -> configRead() -> configParseFile() reads L:/config.json (./config.json on the PC
// build) with ArduinoJson and hands each section to guiSetConfig / haspSetConfig / mqttSetConfig /
// PosixDevice::set_config.
//
// Per input: write the bytes to ./config.json in the (scratch) working directory, run the real
// startup loader, delete the file, then restore the default settings captured at init
// (hasp_fuzz_reset), so the next input starts from the same configuration.
#include "hasp_fuzz_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    hasp_fuzz_init();
    FILE* f = fopen("config.json", "wb");
    if(!f) return 0;
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    if(written == size) {
        configSetup();
        hasp_fuzz_tasks_no_render();
    }
    unlink("config.json");
    hasp_fuzz_reset();
    return 0;
}
