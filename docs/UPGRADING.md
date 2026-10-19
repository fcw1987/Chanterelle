# Upgrading to Chanterelle

Chanterelle 1.1.0 (build 5) continues the existing macOS equalizer. Your tone values, selected output, and saved custom preset stay in the same local preferences domain. Every launch still begins with tuning off.

1. Download the new arm64 ZIP from [GitHub Releases](https://github.com/fcw1987/Chanterelle/releases/latest), verify its checksum, and extract it.
2. Quit the previous app from its menu bar menu before opening Chanterelle. Do not force quit it during active audio unless you are intentionally testing recovery.
3. Put `Chanterelle.app` in your user Applications folder if desired. Keep the previous app elsewhere until you have checked your saved settings; the release does not delete it or any preference files.
4. Open Chanterelle normally. Confirm your output and settings before starting tuning. macOS may ask for first-open or audio capture approval again after an app name or path change.

The source installer refuses to install while an earlier or current copy is running. It also asks you to move a differently named earlier app out of either Applications folder before installing, so Launch Services does not confuse copies with the same identity. It validates the destination's bundle identity and stages/verifies the new app before atomic replacement. A conflicting app is left untouched. It does not silently remove a differently named older app.

## Compatibility identifiers

The public app name, executable, Xcode project, source names, repository, and website are Chanterelle. The hidden bundle identifier `local.ultrafinetune.app` intentionally stays unchanged. It identifies the same installed app, preferences domain, and signing requirement as earlier releases. Renaming it would create a new identity and risk stranding saved settings or requiring a separate permission grant.

The local preference keys remain `toneControls`, `myPreset`, and `selectedOutputUID`. Existing Core Audio compatibility identifiers stay stable where necessary. An LG hardware model match remains in source because it identifies real output hardware, rather than product branding. No updater, keychain migration, credential creation, or preference reset is introduced.

Earlier Git commits, tags, signed binaries, release asset names, and developer validation files retain their original names. Those records are preserved; rebuilding or editing a previously signed binary would invalidate its signature. Current releases and current product pages use Chanterelle. The old repository URL continues through GitHub's repository redirect, and the old website paths have separate compatibility redirects.
