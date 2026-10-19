#!/bin/bash
set -euo pipefail

APP_NAME="Chanterelle"
EXECUTABLE_NAME="Chanterelle"
# Compatibility: retain the original bundle/defaults domain and detect old installations safely.
BUNDLE_IDENTIFIER="local.ultrafinetune.app"
LEGACY_APP_NAME="UltraFineTune"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
BUILD_TEMP=""
PUBLISH_TEMP=""
SHOULD_INSTALL=0
SHOULD_RUN=0
RUN_LOCAL=0
OUTPUT_DIR=""
RELEASE_SIGN_IDENTITY=""
HARDENED_RUNTIME=0

usage() {
    cat <<'USAGE'
Usage: bash build.sh [--install] [--run] [--run-local] [--output-dir path]
                     [--hardened-runtime | --release-sign-identity identity]

No options    Build and sign dist/Chanterelle.app.
--install     Also install into your Applications folder.
--run         Also install and open the app.
--run-local   Open the workspace app without installing.
--output-dir  Build into a separate folder (useful while an older copy runs).
--hardened-runtime
              Enable hardened runtime in an ad hoc local test build.
--release-sign-identity identity
              Sign with an existing full Developer ID Application identity;
              enables hardened runtime and requests a secure timestamp.
--help        Show these instructions.

Requires macOS 14.2 or later and Xcode or Apple Command Line Tools
with a macOS SDK that includes Core Audio process taps.
No administrator access or additional packages are required.
Default builds are ad hoc signed. Signing does not notarize or publish an app.
USAGE
}

fail() {
    printf '\n%s\n' "$*" >&2
    exit 1
}

cleanup() {
    if [[ -n "$PUBLISH_TEMP" && -d "$PUBLISH_TEMP" ]]; then
        /bin/rm -rf -- "$PUBLISH_TEMP"
    fi
    if [[ -n "$BUILD_TEMP" && -d "$BUILD_TEMP" ]]; then
        /bin/rm -rf -- "$BUILD_TEMP"
    fi
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

while (( $# )); do
    option="$1"
    shift
    case "$option" in
        --install) SHOULD_INSTALL=1 ;;
        --run) SHOULD_INSTALL=1; SHOULD_RUN=1 ;;
        --run-local) RUN_LOCAL=1 ;;
        --output-dir) (( $# )) || fail "--output-dir requires a path"; OUTPUT_DIR="$1"; shift ;;
        --hardened-runtime) HARDENED_RUNTIME=1 ;;
        --release-sign-identity)
            (( $# )) || fail "--release-sign-identity requires a full Developer ID Application identity"
            [[ -z "$RELEASE_SIGN_IDENTITY" ]] || fail "--release-sign-identity may be specified only once"
            RELEASE_SIGN_IDENTITY="$1"
            shift
            [[ "$RELEASE_SIGN_IDENTITY" == "Developer ID Application: "?* ]] ||
                fail "Release identity must start with 'Developer ID Application: ' followed by its full certificate name"
            HARDENED_RUNTIME=1
            ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; fail "Unknown option: $option" ;;
    esac
done

PLATFORM="$(uname -s)"
[[ "$PLATFORM" == "Darwin" ]] ||
    fail "Chanterelle must be built on macOS 14.2 or later. This machine is running $PLATFORM."

MACOS_VERSION="$(/usr/bin/sw_vers -productVersion)"
IFS=. read -r MACOS_MAJOR MACOS_MINOR MACOS_PATCH <<< "$MACOS_VERSION"
MACOS_MINOR="${MACOS_MINOR:-0}"
if (( MACOS_MAJOR < 14 || (MACOS_MAJOR == 14 && MACOS_MINOR < 2) )); then
    fail "Chanterelle requires macOS 14.2 or later. This Mac is running $MACOS_VERSION."
fi

if ! /usr/bin/xcode-select -p >/dev/null 2>&1; then
    fail "Apple developer tools were not found. Install Xcode, or run xcode-select --install in Terminal, then run this script again."
fi

if ! CLANG="$(/usr/bin/xcrun --sdk macosx --find clang 2>/dev/null)" ||
   ! SDK_ROOT="$(/usr/bin/xcrun --sdk macosx --show-sdk-path 2>/dev/null)"; then
    fail "The selected Apple developer tools do not provide clang and a macOS SDK. Complete Xcode setup or update Command Line Tools, then try again."
fi

for source_file in Sources/main.m Sources/CHAudioEngine.m Sources/CHAudioEngine.h Sources/CHEQ.c Sources/CHEQ.h Sources/CHRing.c Sources/CHRing.h Sources/CHPresets.h Info.plist Resources/AppIcon.icns; do
    [[ -f "$PROJECT_DIR/$source_file" ]] || fail "A required file is missing: $source_file"
done

BUILD_TEMP="$(/usr/bin/mktemp -d "$PROJECT_DIR/.build.XXXXXX")"
COMMON_FLAGS=(-isysroot "$SDK_ROOT" -mmacosx-version-min=14.2 -O2 -Wall -Wextra -Werror -ffp-contract=off)
OBJC_FLAGS=(-fobjc-arc -fblocks -Werror=unguarded-availability-new)

cat > "$BUILD_TEMP/check_taps.m" <<'OBJC'
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>

static OSStatus checkProcessTapAPI(AudioObjectID *tapID) {
    CATapDescription *description =
        [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
    return AudioHardwareCreateProcessTap(description, tapID);
}
OBJC

if ! "$CLANG" -isysroot "$SDK_ROOT" -mmacosx-version-min=14.2 \
    -fobjc-arc -fblocks -Werror=unguarded-availability-new \
    -fsyntax-only "$BUILD_TEMP/check_taps.m" > "$BUILD_TEMP/sdk_check.log" 2>&1; then
    /bin/cat "$BUILD_TEMP/sdk_check.log" >&2
    fail "The selected macOS SDK does not support the Core Audio tap APIs used by this app. Update Xcode or Command Line Tools and try again."
fi

PLIST_IDENTIFIER="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$PROJECT_DIR/Info.plist")"
[[ "$PLIST_IDENTIFIER" == "$BUNDLE_IDENTIFIER" ]] || fail "The app bundle identifier does not match the installer."

printf 'Building %s for this Mac.\n' "$APP_NAME"
"$CLANG" "${COMMON_FLAGS[@]}" "${OBJC_FLAGS[@]}" -std=gnu11 \
    -I "$PROJECT_DIR/Sources" -c "$PROJECT_DIR/Sources/main.m" -o "$BUILD_TEMP/main.o"
"$CLANG" "${COMMON_FLAGS[@]}" "${OBJC_FLAGS[@]}" -std=gnu11 \
    -I "$PROJECT_DIR/Sources" -c "$PROJECT_DIR/Sources/CHAudioEngine.m" -o "$BUILD_TEMP/CHAudioEngine.o"
for c_source in CHEQ CHRing; do
    "$CLANG" "${COMMON_FLAGS[@]}" -std=c11 \
        -I "$PROJECT_DIR/Sources" -c "$PROJECT_DIR/Sources/$c_source.c" -o "$BUILD_TEMP/$c_source.o"
done

BUILT_APP="$BUILD_TEMP/$APP_NAME.app"
/bin/mkdir -p "$BUILT_APP/Contents/MacOS" "$BUILT_APP/Contents/Resources"
/bin/cp "$PROJECT_DIR/Info.plist" "$BUILT_APP/Contents/Info.plist"
/bin/cp "$PROJECT_DIR/Resources/AppIcon.icns" "$BUILT_APP/Contents/Resources/AppIcon.icns"
"$CLANG" "${COMMON_FLAGS[@]}" -fobjc-arc -fblocks \
    "$BUILD_TEMP/main.o" "$BUILD_TEMP/CHAudioEngine.o" "$BUILD_TEMP/CHEQ.o" "$BUILD_TEMP/CHRing.o" \
    -framework AppKit -framework Foundation -framework CoreAudio -framework AudioToolbox -framework AudioUnit \
    -o "$BUILT_APP/Contents/MacOS/$EXECUTABLE_NAME"
/bin/chmod 755 "$BUILT_APP/Contents/MacOS/$EXECUTABLE_NAME"
/usr/bin/plutil -lint "$BUILT_APP/Contents/Info.plist"
SIGN_FLAGS=(--force --sign - --timestamp=none)
if [[ -n "$RELEASE_SIGN_IDENTITY" ]]; then
    SIGN_FLAGS=(--force --sign "$RELEASE_SIGN_IDENTITY" --timestamp)
    printf 'Signing with the supplied Developer ID Application identity. Notarization is a separate step.\n'
fi
if (( HARDENED_RUNTIME )); then
    SIGN_FLAGS+=(--options runtime)
fi
/usr/bin/codesign "${SIGN_FLAGS[@]}" "$BUILT_APP"
/usr/bin/codesign --verify --strict "$BUILT_APP"

# Publishing uses an atomic filesystem rename. Existing recognized copies are
# swapped only after the new bundle has been copied and its signature verified.
cat > "$BUILD_TEMP/publish_app.c" <<'C'
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 4) {
        fputs("Invalid application replacement arguments.\n", stderr);
        return 64;
    }
    unsigned int flags = strcmp(argv[1], "replace") == 0 ? RENAME_SWAP : RENAME_EXCL;
    if (renamex_np(argv[2], argv[3], flags) != 0) {
        perror("Unable to publish the application atomically");
        return 1;
    }
    return 0;
}
C
"$CLANG" "${COMMON_FLAGS[@]}" "$BUILD_TEMP/publish_app.c" -o "$BUILD_TEMP/publish_app"

app_is_running() {
    /usr/bin/pgrep -u "$(/usr/bin/id -u)" -x "$EXECUTABLE_NAME" >/dev/null 2>&1 ||
        /usr/bin/pgrep -u "$(/usr/bin/id -u)" -x "$LEGACY_APP_NAME" >/dev/null 2>&1
}

check_legacy_install() {
    local legacy_app="$1"
    if app_is_running; then
        fail "Quit Chanterelle or the previous version from its menu bar menu, then run this script again. The running app was left unchanged."
    fi
    if [[ -e "$legacy_app" || -L "$legacy_app" ]]; then
        fail "An older installation exists at $legacy_app. Quit it, then deliberately move it to a backup location before installing Chanterelle. The older app and saved preferences were left unchanged."
    fi
}

check_existing_app() {
    local destination="$1"
    local existing_identifier=""
    if [[ -e "$destination" || -L "$destination" ]]; then
        [[ -d "$destination" && ! -L "$destination" ]] ||
            fail "The destination already exists and is not a regular app folder: $destination"
        existing_identifier="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$destination/Contents/Info.plist" 2>/dev/null || true)"
        [[ "$existing_identifier" == "$BUNDLE_IDENTIFIER" ]] ||
            fail "The destination belongs to another app and was left unchanged: $destination"
        if app_is_running; then
            fail "Quit Chanterelle or the previous version from its menu bar menu, then run this script again. The running app was left unchanged."
        fi
    fi
}

publish_app() {
    local source_app="$1"
    local destination="$2"
    local destination_parent="$(dirname "$destination")"
    local mode="new"

    [[ ! -L "$destination_parent" ]] || fail "The destination folder is a symbolic link: $destination_parent"
    /bin/mkdir -p "$destination_parent"
    check_existing_app "$destination"
    PUBLISH_TEMP="$(/usr/bin/mktemp -d "$destination_parent/.Chanterelle-stage.XXXXXX")"
    /usr/bin/ditto "$source_app" "$PUBLISH_TEMP/$APP_NAME.app"
    /usr/bin/codesign --verify --strict "$PUBLISH_TEMP/$APP_NAME.app"
    check_existing_app "$destination"
    if [[ -d "$destination" ]]; then
        mode="replace"
    fi
    "$BUILD_TEMP/publish_app" "$mode" "$PUBLISH_TEMP/$APP_NAME.app" "$destination" ||
        fail "The previous app was left unchanged. Use a local Mac filesystem that supports atomic app replacement."
    /bin/rm -rf -- "$PUBLISH_TEMP"
    PUBLISH_TEMP=""
}

DIST_APP="${OUTPUT_DIR:-$PROJECT_DIR/dist}/$APP_NAME.app"
publish_app "$BUILT_APP" "$DIST_APP"
printf '\nBuilt: %s\n' "$DIST_APP"

if (( SHOULD_INSTALL )); then
    check_legacy_install "$HOME/Applications/$LEGACY_APP_NAME.app"
    check_legacy_install "/Applications/$LEGACY_APP_NAME.app"
    INSTALLED_APP="$HOME/Applications/$APP_NAME.app"
    publish_app "$DIST_APP" "$INSTALLED_APP"
    printf 'Installed: %s\n' "$INSTALLED_APP"
    if (( SHOULD_RUN )); then
        /usr/bin/open "$INSTALLED_APP"
        printf 'Opened Chanterelle. Look for its icon in the menu bar.\n'
    fi
elif (( RUN_LOCAL )); then
    if app_is_running; then
        fail "Quit Chanterelle or the previous version before opening this local build. The running app was left unchanged."
    fi
    /usr/bin/open "$DIST_APP"
    printf 'Opened the local app; tuning starts off. No installation was performed.\n'
else
    printf 'To open this build after quitting any older copy: open "%s"\n' "$DIST_APP"
fi
