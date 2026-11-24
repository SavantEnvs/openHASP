#!/usr/bin/env bash
# mayhem/build.sh — build openHASP's native (linux_headless) firmware sources directly with clang
# (upstream uses PlatformIO, which would fetch libraries at build time) plus the libFuzzer harnesses
# and the functional self-test. Library sources come pre-fetched (pinned) from /opt/toolchains/deps.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
: "${OPENHASP_DEPS:=/opt/toolchains/deps}"
export OPENHASP_DEPS SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS SRC

cd "$SRC"
LVGL="$OPENHASP_DEPS/lvgl-0af8c7a/src"
PNG="$OPENHASP_DEPS/lv_lib_png-d42282b/src"
BMP="$OPENHASP_DEPS/lv_lib_bmp-b6ca85d/src"
AJSON="$OPENHASP_DEPS/ArduinoJson-7.4.3/src/src"
PAHO="$OPENHASP_DEPS/paho.mqtt.c-4a939dd/src/src"
LVDRV="$OPENHASP_DEPS/lv_drivers-98653be/src"
for d in "$LVGL" "$PNG" "$BMP" "$AJSON" "$PAHO" "$LVDRV"; do
  [ -d "$d" ] || { echo "missing dependency $d (see mayhem/Dockerfile)" >&2; exit 1; }
done

# Defines and include paths of the PlatformIO env `linux_headless` ([env] + linux_headless.ini),
# with LVGL's allocator routed to malloc (LV_MEM_CUSTOM=1 -> hasp_malloc) so ASan sees LVGL objects.
DEFS=(-DPIOENV='"linux_headless"' -DLOG_MAXIMUM_LEVEL_VERBOSE -DLOG_DEFAULT_LEVEL_WARNING
  -DLOG_TIMESTAMP_SOURCE_SYSTEM -DCONFIG_ARDUHAL_ESP_LOG=1 -DCONFIG_LOG_COLORS=1
  -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DLV_COMP_CONF_INCLUDE_SIMPLE -DLV_SYMBOL_DEF_H
  -DLODEPNG_NO_COMPILE_ALLOCATORS -DLV_PNG_USE_LV_FILESYSTEM=1 -DLV_USE_FILESYSTEM=1
  -DSPIFFS_TEMPORAL_FD_CACHE -DARDUINOJSON_DECODE_UNICODE=1 -DARDUINOJSON_ENABLE_COMMENTS=1
  -DSTREAMUTILS_ENABLE_EEPROM=1 -DHASP_VER_MAJ=0 -DHASP_VER_MIN=7 -DHASP_VER_REV=2
  -DHASP_MODEL='"Linux Headless"' -DHASP_TARGET_PC=1 -DTFT_WIDTH=320 -DTFT_HEIGHT=480
  -DHASP_USE_NULL_DRIVER=1 -DHASP_NUM_PAGES=12 -DHASP_USE_SPIFFS=0 -DHASP_USE_LITTLEFS=0
  -DHASP_USE_EEPROM=0 -DHASP_USE_GPIO=0 -DHASP_USE_CONFIG=1 -DHASP_USE_DEBUG=1
  -DHASP_USE_PNGDECODE=1 -DHASP_USE_BMPDECODE=1 -DHASP_USE_GIFDECODE=0 -DHASP_USE_JPGDECODE=0
  -DHASP_USE_QRCODE=0 -DHASP_USE_MQTT=1 -DMQTT_MAX_PACKET_SIZE=2048 -DHASP_ATTRIBUTE_FAST_MEM=
  -DIRAM_ATTR= -DPROGMEM= -DPOSIX -DPAHO_MQTT_STATIC -DLV_MEM_CUSTOM=1)
INCS=(-I"$SRC/include" -I"$SRC/src" -I"$SRC/lib/CharStream" -I"$SRC/lib/lv_fs_if"
  -I"$SRC/lib/lv_lib_freetype" -I"$SRC/lib/StringStream" -I"$AJSON" -I"$LVDRV" -I"$LVGL"
  -I"$LVGL/src" -I"$BMP" -I"$PNG" -I"$PAHO")

# Source set = build_src_filter of linux_headless (src/ minus log/, hal/, sys/net, drv/touch,
# hasp_filesystem.cpp; main_pc.cpp's main() is replaced by the harness).
mapfile -t APP < <(cd "$SRC" && find src lib/lv_fs_if lib/lv_lib_freetype \( -name '*.c' -o -name '*.cpp' \) \
  -not -path 'src/log/*' -not -path 'src/hal/*' -not -path 'src/sys/net/*' -not -path 'src/drv/touch/*' \
  -not -name hasp_filesystem.cpp -not -name main_pc.cpp | LC_ALL=C sort | sed "s|^|$SRC/|")
mapfile -t LIBS < <( { find "$LVGL/src" -name '*.c'; echo "$PNG/lodepng.c"; echo "$PNG/lv_png.c"; echo "$BMP/lv_bmp.c"; } | LC_ALL=C sort)
PAHO_SRCS=()
for f in Base64 Clients Heap LinkedList Log Messages MQTTAsync MQTTAsyncUtils MQTTPacket MQTTPacketOut \
         MQTTPersistence MQTTPersistenceDefault MQTTProperties MQTTProtocolClient MQTTProtocolOut \
         MQTTReasonCodes MQTTTime OsWrapper Proxy SHA1 SocketBuffer Socket StackTrace Thread Tree utf-8 WebSocket; do
  PAHO_SRCS+=("$PAHO/$f.c")
done

# compile <objdir> <flags...> -- <sources...> : one object per source, in parallel.
compile() {
  local out="$1"; shift
  local flags=()
  while [ "$1" != "--" ]; do flags+=("$1"); shift; done; shift
  mkdir -p "$out"
  printf '%s\n' "$@" | xargs -P "$MAYHEM_JOBS" -I{} bash -c '
    src="$1"; out="$2"; shift 2
    obj="$out/$(printf %s "$src" | sed "s|^/||; s|/|_|g").o"
    # LV_MEM_CUSTOM=1 makes LVGL (C) call hasp_malloc/hasp_free; declare them via hasp_mem.h.
    case "$src" in *.cpp) c="$CXX -std=c++14";; *) c="$CC -include $SRC/include/hasp_mem.h";; esac
    extra=""
    # Third-party libraries from /opt/toolchains/deps (LVGL v7, lodepng, lv_bmp) keep ASan + SanCov
    # but not UBSan: LVGL hits benign UB on every frame (shifting ~mask / 0xFF<<24 in int, setters
    # called through void* callback types), which would halt every input before openHASP code runs.
    case "$src" in "$OPENHASP_DEPS"/*) extra="-fno-sanitize=undefined";; esac
    # Fuzz build: no wall-clock tick thread in the null display driver (see no_tick_thread.h).
    case "$src" in */src/drv/tft/tft_driver_null.cpp)
      [ -z "${HASP_FUZZ_BUILD:-}" ] || extra="$extra -include $SRC/mayhem/harnesses/no_tick_thread.h";; esac
    $c "$@" $extra -c "$src" -o "$obj"' _ {} "$out" "${flags[@]}"
}

build_variant() {  # <dir> <cflags string>
  local dir="$1" cflags="$2"
  rm -rf "$dir"; mkdir -p "$dir"
  # shellcheck disable=SC2086
  compile "$dir/app" $cflags -w "${DEFS[@]}" "${INCS[@]}" -- "${APP[@]}" "${LIBS[@]}"
  # paho (MQTT client; linked but not reached from the dispatch entry points): uninstrumented dependency.
  # paho's CMake generates VersionInfo.h from VersionInfo.h.in; do the same here.
  mkdir -p "$dir/paho-gen"
  sed -e 's/@BUILD_TIMESTAMP@/openhasp-mayhem/; s/@PROJECT_VERSION@/1.3.14/' -e 's/@PROJECT_VERSION_MAJOR@/1/' \
      -e 's/@PROJECT_VERSION_MINOR@/3/; s/@PROJECT_VERSION_PATCH@/14/' "$PAHO/VersionInfo.h.in" > "$dir/paho-gen/VersionInfo.h"
  compile "$dir/paho" -O1 -g -gdwarf-3 -w -D_GNU_SOURCE -DPAHO_MQTT_STATIC -I"$dir/paho-gen" -I"$PAHO" -- "${PAHO_SRCS[@]}"
  rm -f "$dir/libopenhasp.a"
  llvm-ar rcs "$dir/libopenhasp.a" "$dir"/app/*.o "$dir"/paho/*.o
}

# 1) Sanitized + DWARF-3 build of the project, harnesses and standalone reproducers.
FUZZ_DIR="$SRC/build/fuzz"
# -fsanitize=fuzzer-no-link: SanCov edge instrumentation on the project + library objects too.
# HASP_USE_CONSOLE=0: the PC console thread reads stdin and runs dispatch_text_line concurrently with
# the fuzzed call; the fuzz targets must stay single-threaded and deterministic.
export HASP_FUZZ_BUILD=1
build_variant "$FUZZ_DIR" "-O1 $SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS -DHASP_USE_CONSOLE=0"
unset HASP_FUZZ_BUILD
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$FUZZ_DIR/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$FUZZ_DIR/standalone_main.o"
for t in fuzz_jsonl fuzz_text_line fuzz_topic_payload fuzz_image fuzz_font fuzz_config; do
  $CXX -std=c++14 -O1 $SANITIZER_FLAGS $DEBUG_FLAGS -w "${DEFS[@]}" "${INCS[@]}" -DHASP_USE_CONSOLE=0 \
    -fsanitize=fuzzer-no-link -c "$SRC/mayhem/harnesses/$t.cpp" -o "$FUZZ_DIR/$t.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$FUZZ_DIR/$t.o" "$FUZZ_DIR/lsan_off.o" \
    -Wl,--whole-archive "$FUZZ_DIR/libopenhasp.a" -Wl,--no-whole-archive -lm -lpthread -o "$SRC/$t"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link "$FUZZ_DIR/$t.o" "$FUZZ_DIR/standalone_main.o" "$FUZZ_DIR/lsan_off.o" \
    -Wl,--whole-archive "$FUZZ_DIR/libopenhasp.a" -Wl,--no-whole-archive -lm -lpthread -o "$SRC/$t-standalone"
done

# 2) Functional self-test with the project's normal flags (-Os, no sanitizers), run by test.sh.
TEST_DIR="$SRC/build/test"
build_variant "$TEST_DIR" "-Os $COVERAGE_FLAGS"
$CXX -std=c++14 -Os $COVERAGE_FLAGS -w "${DEFS[@]}" "${INCS[@]}" -c "$SRC/mayhem/harnesses/hasp_selftest.cpp" -o "$TEST_DIR/hasp_selftest.o"
$CXX $COVERAGE_FLAGS "$TEST_DIR/hasp_selftest.o" -Wl,--whole-archive "$TEST_DIR/libopenhasp.a" -Wl,--no-whole-archive \
  -lm -lpthread -o "$SRC/build/hasp_selftest"
