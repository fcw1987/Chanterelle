# Validation boundaries

Historical RC2 local validation on macOS arm64: native build with warnings as errors, strict ad hoc signature verification, 16 EQ groups, seven ring groups, and eight preset cases. Off-state self-test creates no audio resources. Isolated UI smoke verifies menu wiring, reopen after closing the window, preset application, comparison, Flat EQ, resizing, and visible footer/status, without capture or saved preference writes.

The earlier RC2 publication pass repeated the focused portable fixtures, actual-source callback/cleanup fixtures, native build, and off-state checks after branding. A screenshot of only the app window documents its controls. GitHub Actions repeats build and focused tests for pushed revisions. CI output is available under the repository’s Actions tab.

Earlier local RC fixtures also exercised sanitizers, nine callback groups, 256 cleanup failure combinations, and ten partial-start checkpoints. Developer machine logs and identifiers are intentionally excluded from public source history. Historical results are not claims about every future revision.

Live permission, listening quality, sustained playback, physical latency, installation/update, sleep/disconnect, quit, and force-quit recovery remain pending in the [Mac checklist](MAC_TEST_CHECKLIST.md). No benchmark claim about whole-app CPU or device latency is made.

Stable 1.0.0 build 3 repeats the native build, strict signature verification, off-state self-test, isolated UI smoke, and publication checks. The metadata and product copy change does not alter DSP or engine code. GitHub Actions runs the repository’s native build and focused fixtures before merge.

## 1.0.1 distribution verification

Version 1.0.1 (build 4), arm64, macOS 14.2+, was archived using the shared Xcode Release scheme and exported after Xcode reported **Ready to Distribute**. The exported app was packaged with `INSTALL.md` and `LICENSE`, and the exact final ZIP was extracted to a clean folder for verification.

- `codesign --verify --deep --strict` passed. The signature identifies Developer ID Application: Franklin Waggoner, team `6668UMZH3Y`, with hardened runtime and a secure timestamp.
- `xcrun stapler validate` passed; the app has a stapled notarization ticket.
- `spctl --assess --type execute --verbose=4` accepted the app with source **Notarized Developer ID**.
- The extracted signed app's `--self-test` reported version 1.0.1, build 4, passed, and no audio resources created.
- `Tests/check_publication.py` passed local HTML-link and public-source checks.

Historical 1.0.1 release ZIP SHA-256: `a18f78f999c0380abc834bd80e978d581df5c62343e0f106265d86011d547348`. Signed code CDHash: `1e7352916a004c583cb9f76a8ab9879a45cc7511`.

These checks establish distribution signature, notarization ticket, Gatekeeper assessment, and off-state behavior. Live process taps under hardened runtime, capture permission, normal quarantined first-open behavior, audible playback, sustained playback, latency, and hardware recovery remain pending. The DSP and audio engine code are unchanged from 1.0.0; prior source fixtures do not establish live acceptance. The original 1.0.0 tag and release assets are preserved.
