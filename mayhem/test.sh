#!/usr/bin/env bash
#
# mayhem/test.sh — RUN the upstream ELVM *whirl-backend* functional suite (built by mayhem/build.sh).
#
# For each test program the suite runs the whirl interpreter (out/whirl) on the compiled .eir.whirl
# program and compares its OUTPUT against the frozen golden produced by ELVM's own reference
# interpreter (eli, .eir.out). This is a behavioural / golden-diff oracle: it asserts the interpreter
# produces the CORRECT output, so a no-op / exit(0) sabotage of whirl makes the actual output diverge
# from the golden and FAILS the suite. Emits a CTRF report and exits non-zero iff any test failed.
#
# It does NOT compile — build.sh already built the binaries and froze the goldens; this only runs
# whirl (fresh, each invocation) and diffs.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
SRC="${SRC:-/mayhem}"
cd "$SRC"

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

LIST="out/whirl-tests.list"
if [ ! -f "$LIST" ] || [ ! -x out/whirl ] || [ ! -x runtest.sh ]; then
  echo "test.sh: prerequisites missing (run mayhem/build.sh first)" >&2
  emit_ctrf "elvm-whirl" 0 1 0
  exit 1
fi

passed=0; failed=0; skipped=0; failnames=""
while IFS= read -r act; do
  [ -n "$act" ] || continue
  prog="${act%.out}"                # out/<name>.eir.whirl  (the compiled whirl program)
  exp="${act%.whirl.out}.out"       # out/<name>.eir.out    (frozen golden from eli)
  if [ ! -f "$prog" ] || [ ! -f "$exp" ]; then
    skipped=$((skipped+1)); continue
  fi
  rm -f "$act"
  # runtest.sh runs `out/whirl <prog>` (and feeds any matching test/*.in on stdin) -> actual output.
  if ! ./runtest.sh "$act" out/whirl "$prog" >/dev/null 2>&1; then
    failed=$((failed+1)); failnames="$failnames $(basename "$prog")"; continue
  fi
  if diff -q "$exp" "$act" >/dev/null 2>&1; then
    passed=$((passed+1))
  else
    failed=$((failed+1)); failnames="$failnames $(basename "$prog")"
  fi
done < "$LIST"

[ -n "$failnames" ] && echo "test.sh: FAILED whirl tests:$failnames" >&2
emit_ctrf "elvm-whirl" "$passed" "$failed" "$skipped" 0 0
