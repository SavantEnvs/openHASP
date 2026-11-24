#!/usr/bin/env bash
# mayhem/test.sh — run the openHASP known-answer self-test built by mayhem/build.sh
# (jsonl object creation, pXbY.attr=value commands, json arrays, topic/payload, page/clearpage),
# asserting each value, and report CTRF counts.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"
EXPECTED=15
# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

BIN="$SRC/build/hasp_selftest"
if [ ! -x "$BIN" ]; then
  echo "missing $BIN (mayhem/build.sh must build it)" >&2
  emit_ctrf "hasp-selftest" 0 "$EXPECTED" 0
  exit 1
fi
work=$(mktemp -d)
out=$(cd "$work" && TMPDIR="$work" timeout 120 "$BIN" 2>&1 >/dev/null)
rc=$?
rm -rf "$work"
printf '%s\n' "$out" | grep '^RESULT'
passed=$(printf '%s\n' "$out" | grep -c '^RESULT PASS ')
failed=$(printf '%s\n' "$out" | grep -c '^RESULT FAIL ')
done_line=$(printf '%s\n' "$out" | grep -c '^RESULT DONE failures=0$')
# Checks that never reported (crash / early exit / neutered binary) count as failures.
missing=$(( EXPECTED - passed - failed ))
[ "$missing" -gt 0 ] && failed=$(( failed + missing ))
if [ "$rc" -ne 0 ] || [ "$done_line" -ne 1 ]; then [ "$failed" -eq 0 ] && failed=1; fi
emit_ctrf "hasp-selftest" "$passed" "$failed" 0
