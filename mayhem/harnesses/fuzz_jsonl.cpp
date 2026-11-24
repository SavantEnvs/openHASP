// jsonl object definitions (pages.jsonl / the `jsonl` MQTT command):
// dispatch_parse_jsonl -> hasp_new_object -> hasp_process_obj_attribute -> LVGL.
#include "hasp_fuzz_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    hasp_fuzz_init();
    char* buf = hasp_fuzz_cstr(data, size);
    dispatch_parse_jsonl(NULL, buf, TAG_MQTT);
    hasp_fuzz_task_handler();
    hasp_fuzz_reset();
    free(buf);
    return 0;
}
