#!/bin/bash
# Reproducible safe RC validation. All audio content is generated in memory.
# Never installs an app, starts capture/playback, or mutates devices/preferences.
set -euo pipefail

CH_RC_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
CH_RC_RESULTS="${CH_RC_RESULTS_DIR:-$CH_RC_ROOT/Tests/results/$(date -u +%Y%m%dT%H%M%SZ)}"
CH_RC_BUILD="$(mktemp -d "${TMPDIR:-/tmp}/ch-rc-tests.XXXXXX")"
trap 'rm -rf "$CH_RC_BUILD"' EXIT
mkdir -p "$CH_RC_RESULTS"
CH_RC_FAILED=0

run_phase() {
    local phase="$1"
    shift
    printf '\nRunning %s\n' "$phase"
    if "$@" >"$CH_RC_RESULTS/$phase.log" 2>&1; then
        cat "$CH_RC_RESULTS/$phase.log"
        printf 'PASS %s\n' "$phase" | tee -a "$CH_RC_RESULTS/summary.txt"
    else
        local result=$?
        cat "$CH_RC_RESULTS/$phase.log"
        printf 'FAIL %s (exit %d)\n' "$phase" "$result" | tee -a "$CH_RC_RESULTS/summary.txt"
        CH_RC_FAILED=1
    fi
}

{
    date -u
    uname -sm
    git -C "$CH_RC_ROOT" rev-parse HEAD
    git -C "$CH_RC_ROOT" status --short
    for CH_RC_SOURCE in CHEQ.c CHRing.c CHAudioEngine.m; do
        shasum -a 256 "$CH_RC_ROOT/Sources/$CH_RC_SOURCE"
    done
    if [[ -n "${CH_RC_APP_BINARY:-}" ]]; then
        shasum -a 256 "$CH_RC_APP_BINARY"
    fi
    "${CC:-cc}" --version
    python3 --version
    if [[ "$(uname -s)" == Darwin ]]; then
        sw_vers
        xcrun --sdk macosx --show-sdk-version
    fi
} > "$CH_RC_RESULTS/environment.txt" 2>&1
: > "$CH_RC_RESULTS/summary.txt"

run_phase portable-normal env CHEQ_SANITIZE= "$CH_RC_ROOT/Tests/run_tests.sh"
run_phase portable-asan-ubsan env ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" CHEQ_SANITIZE=address "$CH_RC_ROOT/Tests/run_tests.sh"
run_phase portable-tsan env CHEQ_SANITIZE=thread "$CH_RC_ROOT/Tests/run_tests.sh"
run_phase callbacks-normal python3 "$CH_RC_ROOT/Tests/check_callbacks.py"
run_phase callbacks-asan-ubsan python3 "$CH_RC_ROOT/Tests/check_callbacks.py" --sanitize
run_phase callbacks-tsan python3 "$CH_RC_ROOT/Tests/check_callbacks.py" --thread-sanitize

if [[ "$(uname -s)" == Darwin ]]; then
    run_phase install-guards python3 "$CH_RC_ROOT/Tests/check_install_guards.py"
    run_phase lifecycle-normal python3 "$CH_RC_ROOT/Tests/check_lifecycle.py"
    run_phase lifecycle-asan-ubsan python3 "$CH_RC_ROOT/Tests/check_lifecycle.py" --sanitize
else
    printf 'SKIP lifecycle: macOS Foundation runtime required\n' | tee -a "$CH_RC_RESULTS/summary.txt"
fi

run_phase benchmark-build "${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -fno-fast-math \
    -I"$CH_RC_ROOT/Sources" "$CH_RC_ROOT/Tests/benchmark_dsp.c" \
    "$CH_RC_ROOT/Sources/CHEQ.c" "$CH_RC_ROOT/Sources/CHRing.c" -lm -o "$CH_RC_BUILD/benchmark"
if [[ -x "$CH_RC_BUILD/benchmark" ]]; then
    run_phase benchmark "$CH_RC_BUILD/benchmark"
fi

# A caller may explicitly supply the native binary after a successful build.
# These CLI modes must remain read-only/off-state as documented by the app.
if [[ -n "${CH_RC_APP_BINARY:-}" ]]; then
    run_phase native-self-test "$CH_RC_APP_BINARY" --self-test
    run_phase native-diagnostics "$CH_RC_APP_BINARY" --diagnostics
fi
printf '\nEvidence: %s\n' "$CH_RC_RESULTS"
exit "$CH_RC_FAILED"
