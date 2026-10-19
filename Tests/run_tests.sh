#!/bin/sh
set -eu

CH_TEST_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CH_TEST_BUILD=$(mktemp -d "${TMPDIR:-/tmp}/cheq-tests.XXXXXX")
trap 'rm -rf "$CH_TEST_BUILD"' EXIT HUP INT TERM
CH_TEST_CC=${CC:-cc}
CH_SANITIZER_FLAGS=

case "${CHEQ_SANITIZE:-}" in
    "") ;;
    address) CH_SANITIZER_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" ;;
    thread) CH_SANITIZER_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" ;;
    *) echo "CHEQ_SANITIZE must be empty, address, or thread" >&2; exit 2 ;;
esac

for CH_TEST_SUITE in eq ring presets; do
    case "$CH_TEST_SUITE" in
        eq) CH_TEST_SOURCE=CHEQ ;;
        ring) CH_TEST_SOURCE=CHRing ;;
        presets) CH_TEST_SOURCE=CHEQ ;;
    esac
    # Intentional word splitting expands the fixed optional compiler flags above.
    # shellcheck disable=SC2086
    "$CH_TEST_CC" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -g -fno-fast-math \
        $CH_SANITIZER_FLAGS \
        -I"$CH_TEST_ROOT/Sources" \
        "$CH_TEST_ROOT/Sources/$CH_TEST_SOURCE.c" \
        "$CH_TEST_ROOT/Tests/test_$CH_TEST_SUITE.c" \
        -lm -pthread -o "$CH_TEST_BUILD/test_$CH_TEST_SUITE"

    "$CH_TEST_BUILD/test_$CH_TEST_SUITE"
done
