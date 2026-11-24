// Image files referenced by an `img` object's `src` attribute: the input becomes ./f.png
// (first byte even) or ./f.bmp (odd), an img object loads it through the L: (lv_fs_pc) drive, and a
// frame is rendered so LVGL's image cache runs the lv_lib_png (lodepng) / lv_lib_bmp decoder.
#include "hasp_fuzz_common.h"

static bool write_file(const char* name, const uint8_t* data, size_t size)
{
    FILE* f = fopen(name, "wb");
    if(!f) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    hasp_fuzz_init();
    if(size < 1) return -1;
    bool bmp = data[0] & 1;
    if(!write_file(bmp ? "f.bmp" : "f.png", data + 1, size - 1)) return -1;
    dispatch_parse_jsonl(NULL,
                         bmp ? "{\"page\":1,\"id\":1,\"obj\":\"img\",\"src\":\"L:/f.bmp\",\"auto_size\":1}"
                             : "{\"page\":1,\"id\":1,\"obj\":\"img\",\"src\":\"L:/f.png\",\"auto_size\":1}",
                         TAG_MQTT);
    lv_refr_now(NULL);
    hasp_fuzz_reset();
    lv_img_cache_invalidate_src(NULL); // the cache is keyed by path; the next input reuses it
    return 0;
}
