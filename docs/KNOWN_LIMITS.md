# Known limits and validation

- macOS 14.2+ with a Core Audio process-tap SDK. Native local evidence is arm64; Intel live playback is not validated.
- The selected output must be a supported stereo physical device already selected as the Mac’s default, including eligible LG display speakers. LG acoustic calibration and model-wide hardware acceptance are not supplied. Unsupported layouts are rejected. Capture and playback need matching nominal sample rates.
- Presets are subjective starting points, not measured acoustic calibration or LG endorsement.
- Buffering adds latency. The displayed target is not measured end-to-end latency.
- Protected content and direct output routing may not be captured.
- Compare original tone retains trim and headroom. Stop tuning restores ordinary playback.
- Sample clamping bounds output but cannot guarantee distortion-free processing for every input.
- Route changes and sleep stop tuning; confirm the route and restart manually.
- One custom preset is stored. No preset library, sync, automatic updater, or automatic tuning startup.
- Default source builds are ad hoc signed. The Developer ID signed and notarized 1.1.0 release ZIP supports Apple silicon (arm64) only; no Intel or universal binary is supplied.

## Verified and pending

See the [validation summary](../VALIDATION.md) for native build, off-state checks, and automated fixture coverage. Fixtures establish source behavior and bookkeeping, not Apple’s live audio lifecycle.

Live permission, audible stereo processing, listening quality, physical latency, sustained playback, installation/update behavior, disconnect/format changes, sleep, quit, and force-quit recovery remain pending. Complete the [Mac acceptance checklist](../MAC_TEST_CHECKLIST.md) before claiming acceptance.

Public source is MIT licensed. Local test output, hardware diagnostics, and developer machine evidence are excluded from public history.
