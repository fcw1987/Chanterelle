# Build and installation

## Download 1.1.0

The [1.1.0 release](https://github.com/fcw1987/Chanterelle/releases/tag/v1.1.0) provides a runnable arm64 app ZIP and SHA-256 checksum. Extract the ZIP, read `INSTALL.md`, quit an older copy, and open the app. Moving it to your user’s Applications folder is optional. The download needs macOS 14.2+ on Apple silicon; no developer tools are required to run it.

The downloadable 1.1.0 app is Developer ID signed and Apple notarized, with hardened runtime, a secure timestamp, and a stapled ticket. Open the app normally and confirm macOS’s first-open prompt. This is separate from system audio capture permission, which the app requests when you start tuning. Default source builds remain ad hoc signed.

## Source build requirements

- macOS 14.2 or later.
- Xcode or Apple Command Line Tools with a macOS SDK that includes Core Audio process taps.
- The repository’s source files and `Info.plist`.
- Python 3 only if running the extracted test fixtures.

The build script checks the operating system, compiler, SDK API availability, and bundle identifier. If developer tools are absent, install Xcode or run `xcode-select --install`, complete setup, and retry. Builds use Apple frameworks and in-tree C/Objective-C source, with no package-manager dependencies.

## Commands

Run these from the repository folder:

```sh
bash build.sh             # Build dist/Chanterelle.app only.
bash build.sh --run-local # Build and open the workspace app.
bash build.sh --install   # Build and install into ~/Applications.
bash build.sh --run       # Build, install, and open.
bash build.sh --help      # Show options.
bash build.sh --output-dir /tmp/Chanterelle-review # Separate build output.
```

`BuildAndRun.command` also builds and opens the workspace app, without installation. It displays failures in Terminal. An installed app lives at `~/Applications/Chanterelle.app`; the executable is `Contents/MacOS/Chanterelle` inside either bundle.

The script builds for the current Mac’s architecture. The recorded native RC evidence is arm64; it does not establish an Intel build or live Intel playback result. The build enables compiler warnings as errors and disables floating-point contraction for the finite-input EQ regression.

## Update and signing

Quit a running copy before replacing it. The installer checks the existing bundle identifier, verifies a staged replacement, and uses an atomic filesystem replacement for a recognized app. A conflicting app at the same destination is left unchanged. Installation does not need administrator access.

Default builds receive an ad hoc local signature and strict signature verification. This is not Developer ID signing or notarization. `--hardened-runtime` adds hardened runtime for local compatibility testing. `--release-sign-identity` uses an existing Developer ID Application identity, enables hardened runtime, and requests a secure timestamp. Signing does not notarize or publish the app; see the [release signing checklist](RELEASE_SIGNING.md).

macOS can ask for capture permission again after rebuilding; keeping a stable installed path helps avoid unnecessary identity changes but does not guarantee permission persistence. Do not treat successful signature verification as proof of a trusted publisher.

## Remove

Quit the app and move `~/Applications/Chanterelle.app` to the Trash if installed. Delete the source folder and `dist` output if no longer needed. **Flat EQ** zeroes tone gains and trim and clears comparison mode; it preserves the saved preset, selected output, and whether processing is running. It is not a data deletion control. Removing the app does not itself promise deletion of macOS permission records or preference files.

## Validate

```sh
Tests/run_tests.sh
Tests/run_rc_tests.sh
```

See [the test guide](../Tests/README.md) for sanitizer requirements, result directories, and optional native off-state checks. Complete [Mac acceptance](../MAC_TEST_CHECKLIST.md) before claiming live playback quality or recovery. Prior recorded evidence applies to its recorded source hashes and environment, not automatically to subsequent changes.

## Informational website

Open `docs/index.html` directly in a browser, or serve the repository using a local static server. No build step, JavaScript, CDN, or external font is needed. CSS and the SVG icon live in `docs/assets/`. GitHub Pages can serve the `docs` directory from a published branch. The site uses local assets and links to this project on GitHub. GitHub Pages publishes the main branch’s /docs folder, matching the repository’s saved Pages setting. Native build/fixture CI validates pull requests before review and merge. The Markdown guides are also available as source files alongside the site.

For the 1.1.0 name change, read [upgrading safely](UPGRADING.md). The installer preserves earlier app files and preferences; it does not uninstall a differently named copy. Quit that copy before installing or launching Chanterelle.
