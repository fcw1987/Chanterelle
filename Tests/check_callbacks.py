#!/usr/bin/env python3
"""Check the current native engine's portable C callback logic.

Run from any directory:
    python3 Tests/check_callbacks.py
    python3 Tests/check_callbacks.py --sanitize

The second command enables AddressSanitizer and UndefinedBehaviorSanitizer.
Leak detection is disabled by default because some managed runtimes block its
process inspection. Set ASAN_OPTIONS explicitly to override that default.

This does not compile Objective C, call Apple frameworks, or test a real device.
It extracts the actual current callback code instead of keeping a second copy.
Only Python's standard library and a C11 compiler are required. CC selects the
compiler, with cc as the default.
"""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sanitizer = parser.add_mutually_exclusive_group()
    sanitizer.add_argument("--sanitize", action="store_true")
    sanitizer.add_argument("--thread-sanitize", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    source = (root / "Sources" / "CHAudioEngine.m").read_text()
    start_marker = "typedef struct {\n    float *left;"
    end_marker = "@interface CHAudioEngine"
    if source.count(start_marker) != 1 or source.count(end_marker) != 1:
        raise SystemExit("The callback extraction boundaries changed. Update this test runner.")
    start = source.index(start_marker)
    end = source.index(end_marker, start)
    callbacks = source[start:end]
    if "CHCaptureRender" not in callbacks or "CHOutputRender" not in callbacks:
        raise SystemExit("Both native callbacks must be present in the extracted source.")
    fixture = (root / "Tests" / "callback_fixture.c").read_text()
    fixture_marker = "/* CHAUDIO_CALLBACKS */"
    if fixture.count(fixture_marker) != 1:
        raise SystemExit("The callback fixture must contain exactly one insertion marker.")
    compiler = shlex.split(os.environ.get("CC", "cc"))
    if not compiler:
        raise SystemExit("CC must name a C11 compiler.")
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror"]
    if args.thread_sanitize:
        flags += ["-O1", "-g", "-fsanitize=thread", "-fno-omit-frame-pointer"]
    elif args.sanitize:
        flags += ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    else:
        flags += ["-O2"]
    environment = os.environ.copy()
    if args.sanitize:
        environment.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    with tempfile.TemporaryDirectory(prefix="chanterelle_callback_checks_") as directory:
        scratch = Path(directory)
        test_source = scratch / "callbacks.c"
        test_source.write_text(fixture.replace(fixture_marker, callbacks))
        executable = scratch / "callbacks"
        command = compiler + flags + [
            "-I", str(root / "Sources"), str(test_source),
            str(root / "Sources" / "CHEQ.c"), str(root / "Sources" / "CHRing.c"),
            "-lm", "-pthread", "-o", str(executable),
        ]
        subprocess.run(command, check=True, env=environment)
        subprocess.run([str(executable)], check=True, env=environment)
    print("Nine portable callback groups passed. Apple API and device checks require macOS.")


if __name__ == "__main__":
    main()
