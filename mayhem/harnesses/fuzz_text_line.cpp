// Command lines (MQTT `command` payload, console, .cmd scripts): dispatch_text_line per line,
// against a fixed page of objects so pXbY.attr=value / pXbY.attr references resolve.
#include "hasp_fuzz_common.h"

static const char kPreload[] =
    "{\"page\":1,\"id\":1,\"obj\":\"label\",\"text\":\"label\"}\n"
    "{\"page\":1,\"id\":2,\"obj\":\"btn\",\"text\":\"btn\",\"toggle\":true}\n"
    "{\"page\":1,\"id\":3,\"obj\":\"slider\",\"min\":0,\"max\":100,\"val\":5}\n"
    "{\"page\":1,\"id\":4,\"obj\":\"dropdown\",\"options\":\"a\\nb\\nc\"}\n"
    "{\"page\":1,\"id\":5,\"obj\":\"line\",\"points\":[[0,0],[10,10]]}\n"
    "{\"page\":1,\"id\":6,\"obj\":\"arc\",\"min\":0,\"max\":100,\"val\":5}\n"
    "{\"page\":1,\"id\":7,\"obj\":\"roller\",\"options\":\"x\\ny\"}\n"
    "{\"page\":1,\"id\":8,\"obj\":\"btnmatrix\",\"options\":[\"1\",\"2\",\"3\"]}\n"
    "{\"page\":1,\"id\":9,\"obj\":\"textarea\",\"text\":\"ta\"}\n"
    "{\"page\":1,\"id\":10,\"obj\":\"checkbox\",\"text\":\"cb\"}\n"
    "{\"page\":1,\"id\":11,\"obj\":\"gauge\",\"val\":5}\n"
    "{\"page\":1,\"id\":12,\"obj\":\"cpicker\"}\n"
    "{\"page\":1,\"id\":13,\"obj\":\"bar\",\"val\":5}\n"
    "{\"page\":2,\"id\":1,\"obj\":\"obj\",\"x\":10,\"y\":10}\n";

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    hasp_fuzz_init();
    dispatch_parse_jsonl(NULL, kPreload, TAG_MQTT);
    char* buf = hasp_fuzz_cstr(data, size);
    char* save = NULL;
    for(char* line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        dispatch_text_line(line, TAG_MQTT);
    }
    hasp_fuzz_tasks_no_render();
    hasp_fuzz_reset();
    free(buf);
    return 0;
}
