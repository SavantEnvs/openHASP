// Functional known-answer checks for the openHASP command/object layer (built with normal flags,
// run by mayhem/test.sh). Prints "RESULT PASS <name>" / "RESULT FAIL <name> ..." on stderr.
#include "hasp_fuzz_common.h"

static int failures = 0;
static void check(bool ok, const char* name, const char* detail = "")
{
    fprintf(stderr, "RESULT %s %s %s\n", ok ? "PASS" : "FAIL", name, detail);
    if(!ok) failures++;
}
static const char* label_text(uint8_t page, uint8_t id)
{
    lv_obj_t* o = hasp_find_obj_from_page_id(page, id);
    return o ? lv_label_get_text(o) : "<none>";
}

int main()
{
    hasp_fuzz_init();
    dispatch_parse_jsonl(NULL,
                         "{\"page\":1,\"id\":1,\"obj\":\"label\",\"text\":\"Hello\",\"x\":5,\"y\":7,\"w\":100}\n"
                         "{\"page\":1,\"id\":2,\"obj\":\"slider\",\"min\":0,\"max\":50,\"val\":25}\n"
                         "{\"page\":2,\"id\":3,\"obj\":\"arc\",\"min\":10,\"max\":20,\"val\":15}\n",
                         TAG_MQTT);
    lv_task_handler();

    check(hasp_find_obj_from_page_id(1, 1) != NULL, "jsonl_creates_label");
    check(strcmp(label_text(1, 1), "Hello") == 0, "jsonl_label_text", label_text(1, 1));
    lv_obj_t* l = hasp_find_obj_from_page_id(1, 1);
    check(l && lv_obj_get_x(l) == 5 && lv_obj_get_y(l) == 7, "jsonl_label_xy");
    check(l && lv_obj_get_width(l) == 100, "jsonl_label_w");
    lv_obj_t* s = hasp_find_obj_from_page_id(1, 2);
    check(s && lv_slider_get_value(s) == 25, "jsonl_slider_val");
    lv_obj_t* a = hasp_find_obj_from_page_id(2, 3);
    check(a && lv_arc_get_value(a) == 15 && lv_arc_get_min_value(a) == 10, "jsonl_arc_range");
    check(hasp_find_obj_from_page_id(1, 9) == NULL, "missing_obj_is_null");

    dispatch_text_line("p1b1.text=World", TAG_MQTT);
    check(strcmp(label_text(1, 1), "World") == 0, "cmd_set_text", label_text(1, 1));
    dispatch_text_line("p1b2.val=40", TAG_MQTT);
    check(s && lv_slider_get_value(s) == 40, "cmd_set_val");
    dispatch_text_line("p1b1.w=123", TAG_MQTT);
    check(l && lv_obj_get_width(l) == 123, "cmd_set_w");
    dispatch_text_line("[\"p1b1.text=Json\",\"p1b2.val=7\"]", TAG_MQTT);
    check(strcmp(label_text(1, 1), "Json") == 0 && lv_slider_get_value(s) == 7, "json_array_cmds");
    dispatch_text_line("json [\"p1b1.text=Cmd\"]", TAG_MQTT);
    check(strcmp(label_text(1, 1), "Cmd") == 0, "json_command", label_text(1, 1));
    dispatch_topic_payload("p1b1.text", "Topic", true, TAG_MQTT);
    check(strcmp(label_text(1, 1), "Topic") == 0, "topic_payload_set", label_text(1, 1));
    dispatch_text_line("page 2", TAG_MQTT);
    check(haspPages.get() == 2, "cmd_page");
    dispatch_text_line("clearpage 1", TAG_MQTT);
    lv_task_handler();
    check(hasp_find_obj_from_page_id(1, 1) == NULL && hasp_find_obj_from_page_id(2, 3) != NULL, "cmd_clearpage");

    fprintf(stderr, "RESULT DONE failures=%d\n", failures);
    return failures ? 1 : 0;
}
