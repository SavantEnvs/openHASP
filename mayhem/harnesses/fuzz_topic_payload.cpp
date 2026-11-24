// MQTT message routing as mqtt_message_cb hands it over: topic (first line) + payload (rest)
// into dispatch_topic_payload (command/..., config/..., output/... topics).
#include "hasp_fuzz_common.h"

static const char kPreload[] =
    "{\"page\":1,\"id\":1,\"obj\":\"label\",\"text\":\"label\"}\n"
    "{\"page\":1,\"id\":2,\"obj\":\"btn\",\"text\":\"btn\"}\n"
    "{\"page\":1,\"id\":3,\"obj\":\"slider\",\"min\":0,\"max\":100,\"val\":5}\n"
    "{\"page\":1,\"id\":4,\"obj\":\"dropdown\",\"options\":\"a\\nb\"}\n";

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    hasp_fuzz_init();
    dispatch_parse_jsonl(NULL, kPreload, TAG_MQTT);
    char* buf = hasp_fuzz_cstr(data, size);
    char* payload = strchr(buf, '\n');
    if(payload) *payload++ = 0;
    else payload = buf + size;
    dispatch_topic_payload(buf, payload, strlen(payload) > 0, TAG_MQTT);
    hasp_fuzz_tasks_no_render();
    hasp_fuzz_reset();
    free(buf);
    return 0;
}
