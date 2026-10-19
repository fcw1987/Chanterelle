# Release signing

Chanterelle uses the existing Developer ID Application identity, signing team, and bundle identifier. Release 1.1.0 (build 5) is packaged for Apple silicon and macOS 14.2+. See [validation evidence](../VALIDATION.md) for the exact release ZIP, signature, notarization, and checksum results. Signing alone does not establish notarization.

## Local source builds

```sh
bash build.sh --hardened-runtime --output-dir /tmp/Chanterelle-runtime-review
/tmp/Chanterelle-runtime-review/Chanterelle.app/Contents/MacOS/Chanterelle --self-test
codesign --verify --strict /tmp/Chanterelle-runtime-review/Chanterelle.app
```

Default builds are ad hoc signed. `--hardened-runtime` adds runtime protection for local checks, without a trusted publisher or secure timestamp. Self-test stays off and creates no audio resources.

## Sign and archive

Use an existing authorized Developer ID Application identity and its private key. Do not create credentials or export keys as part of a release. The command line builder accepts the full existing certificate name:

```sh
bash build.sh --release-sign-identity 'Developer ID Application: FULL EXISTING NAME (TEAMID)' --output-dir /tmp/Chanterelle-release-candidate
```

The shared Xcode Release scheme uses manual Developer ID signing, hardened runtime, a secure timestamp, arm64, and macOS 14.2. It does not add sandbox or microphone entitlements:

```sh
xcodebuild -project Chanterelle.xcodeproj -scheme Chanterelle \
  -configuration Release -destination 'generic/platform=macOS' \
  -derivedDataPath /tmp/Chanterelle-derived \
  -archivePath /tmp/Chanterelle.xcarchive \
  -disableAutomaticPackageResolution archive \
  DEVELOPMENT_TEAM=EXISTING_TEAM_ID \
  CODE_SIGN_IDENTITY='Developer ID Application: FULL EXISTING NAME (TEAMID)'
```

Keep the compatibility identity described in [upgrading](UPGRADING.md). Verify the expected Developer ID authority, TeamIdentifier, secure Timestamp, and runtime flag. A missing identity, inaccessible private key, or timestamp failure must stop the release; do not fall back to ad hoc signing.

## Notarization and exact-package checks

Use the existing signed-in Xcode distribution account to upload a Developer ID archive for notarization, or an already authorized notarytool Keychain profile. An upload must be authorized by the operator. Do not create a profile or accept a new agreement implicitly. Require Apple's Accepted / Ready to Distribute result, export the notarized app, then validate it:

```sh
xcrun stapler validate /path/to/Chanterelle.app
codesign --verify --deep --strict /path/to/Chanterelle.app
codesign --display --verbose=4 /path/to/Chanterelle.app
spctl --assess --type execute --verbose=4 /path/to/Chanterelle.app
/path/to/Chanterelle.app/Contents/MacOS/Chanterelle --self-test
```

Require a valid stapled ticket and Gatekeeper acceptance as Notarized Developer ID. Package the app with `INSTALL.md` and `LICENSE`, compute SHA-256, extract the exact ZIP into a clean folder, and repeat the checks. Repeat against a fresh public download. Any signed app modification requires rebuilding, signing, and notarizing again.

The off-state checks do not establish live capture permission, listening quality, latency, or hardware recovery. Complete the [Mac acceptance checklist](../MAC_TEST_CHECKLIST.md) with comfortable volume before claiming those results. The rebrand does not start capture or change audio devices to exercise them.

References: [Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution), [custom notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow), and local `xcodebuild -help`, `man codesign`, and `xcrun notarytool --help`.
