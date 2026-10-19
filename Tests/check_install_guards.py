#!/usr/bin/env python3
"""Exercise actual installer guards with temporary bundles; never install or launch."""
from pathlib import Path
import os
import plistlib
import subprocess
import tempfile


root = Path(__file__).resolve().parent.parent
source = (root / "build.sh").read_text()
guards = source[source.index("check_legacy_install() {"):source.index("publish_app() {")]
harness = """set -euo pipefail
APP_NAME=Chanterelle
EXECUTABLE_NAME=Chanterelle
LEGACY_APP_NAME=UltraFineTune
BUNDLE_IDENTIFIER=local.ultrafinetune.app
fail() { printf '%s\\n' "$*" >&2; exit 1; }
app_is_running() { [[ "$CH_TEST_RUNNING" == 1 ]]; }
""" + guards + '\n"$1" "$2"\n'


def check(action, path, running=False, message=None):
    result = subprocess.run(
        ["bash", "-c", harness, "guard-check", action, str(path)],
        env={**os.environ, "CH_TEST_RUNNING": "1" if running else "0"},
        text=True, capture_output=True,
    )
    assert result.returncode == (1 if message else 0), result.stderr
    if message:
        assert message in result.stderr, result.stderr


with tempfile.TemporaryDirectory(prefix="chanterelle-install-guards-") as directory:
    scratch = Path(directory)
    legacy = scratch / "UltraFineTune.app"
    check("check_legacy_install", legacy)
    check("check_legacy_install", legacy, running=True, message="running app was left unchanged")
    legacy.mkdir()
    sentinel = legacy / "preserved-file"
    sentinel.write_text("keep")
    check("check_legacy_install", legacy, message="older app and saved preferences were left unchanged")
    assert sentinel.read_text() == "keep"
    sentinel.unlink()
    legacy.rmdir()
    legacy.symlink_to(scratch / "missing-old-app")
    check("check_legacy_install", legacy, message="older app and saved preferences were left unchanged")
    assert legacy.is_symlink()

    destination = scratch / "Chanterelle.app"
    check("check_existing_app", destination)
    destination.mkdir()
    contents = destination / "Contents"
    contents.mkdir()
    plist = contents / "Info.plist"
    plist.write_bytes(plistlib.dumps({"CFBundleIdentifier": "another.app"}))
    check("check_existing_app", destination, message="belongs to another app and was left unchanged")
    plist.write_bytes(plistlib.dumps({"CFBundleIdentifier": "local.ultrafinetune.app"}))
    original = plist.read_bytes()
    check("check_existing_app", destination)
    check("check_existing_app", destination, running=True, message="running app was left unchanged")
    assert plist.read_bytes() == original
    link = scratch / "app-link"
    link.symlink_to(destination)
    check("check_existing_app", link, message="not a regular app folder")
    assert link.is_symlink()

print("PASS legacy/running/unrecognized/symlink app guards preserve existing files")
