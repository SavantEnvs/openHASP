// Binary LVGL fonts named by the `text_font` attribute: get_font("fz") -> hasp_font_load("L:\fz.bin")
// (src/font/hasp_font_loader.cpp). The input is written to that file in the scratch cwd, a label
// using the font is created and rendered, then `clearfont` drops the cached font.
#include "hasp_fuzz_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    hasp_fuzz_init();
    // lv_fs_open strips "L:" and the following separator, lv_fs_pc prefixes "./": ./fz.bin.
    FILE* f = fopen("fz.bin", "wb");
    if(!f) return -1;
    bool ok = fwrite(data, 1, size, f) == size;
    if(fclose(f) != 0 || !ok) return -1;
    dispatch_parse_jsonl(NULL,
                         "{\"page\":1,\"id\":1,\"obj\":\"label\",\"text\":\"Hello 0123 AVWa !?\",\"text_font\":\"fz\"}\n"
                         "{\"page\":1,\"id\":2,\"obj\":\"btn\",\"text\":\"\xee\x80\x81 ok\",\"text_font\":\"fz\"}",
                         TAG_MQTT);
    lv_refr_now(NULL);
    dispatch_text_line("clearfont", TAG_MQTT);
    hasp_fuzz_reset();
    return 0;
}
