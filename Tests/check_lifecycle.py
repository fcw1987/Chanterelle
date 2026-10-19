#!/usr/bin/env python3
"""Run actual teardown code against fake HAL resources; never call audio APIs.

Foundation/Objective-C is required. This tests our bookkeeping and error paths,
not macOS callback quiescence or the implementation of Apple frameworks.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    source = (root / "Sources/CHAudioEngine.m").read_text()
    start = "- (void)finishWithError:(NSError *)failure notify:(BOOL)notify {"
    end = "- (void)stop {"
    if source.count(start) != 1 or source.count(end) != 1:
        raise SystemExit("The lifecycle extraction boundaries changed. Update the fixture.")
    method = source[source.index(start):source.index(end, source.index(start))]
    fixture = (root / "Tests/lifecycle_fixture.m").read_text()
    marker = "/* CHAUDIO_TEARDOWN */"
    if fixture.count(marker) != 1:
        raise SystemExit("Expected exactly one teardown fixture marker.")
    flags = ["-std=gnu11", "-fobjc-arc", "-fblocks", "-Wall", "-Wextra", "-Werror", "-g"]
    flags += ["-O1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else ["-O2"]
    environment = os.environ.copy()
    if args.sanitize:
        environment.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    with tempfile.TemporaryDirectory(prefix="chanterelle_lifecycle_") as directory:
        scratch = Path(directory)
        test_source = scratch / "lifecycle.m"
        test_source.write_text(fixture.replace(marker, method))
        executable = scratch / "lifecycle"
        command = shlex.split(os.environ.get("CC", "cc")) + flags + [
            "-I", str(root / "Sources"), str(test_source),
            str(root / "Sources/CHEQ.c"), str(root / "Sources/CHRing.c"),
            "-framework", "Foundation", "-lm", "-o", str(executable),
        ]
        subprocess.run(command, check=True, env=environment)
        subprocess.run([str(executable)], check=True, env=environment)


if __name__ == "__main__":
    main()
