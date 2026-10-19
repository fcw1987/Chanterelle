# Privacy

Chanterelle processes captured system playback locally in memory. The current implementation does not save audio recordings, transmit audio over a network, include analytics or telemetry, or use microphone or screen capture APIs. The website contains no scripts, tracking, remote assets, or embedded services.

## Permission

Core Audio process taps require macOS system audio capture permission. The app’s `NSAudioCaptureUsageDescription` explains the local EQ purpose. Capture starts only after you manually click Start tuning; every launch begins with tuning off. System playback capture can include audio from other running apps. Consider that scope when deciding whether to grant permission.

A stereo tap excludes Chanterelle’s own playback to avoid recapturing its processed output. A private aggregate receives captured audio; a separate output Audio Unit sends the result to the selected physical device. The implementation does not activate the display microphone.

## Local data

`NSUserDefaults` stores tone control values, the selected output’s device identifier, and one custom preset under the app’s preferences. **Flat EQ** resets the active tone/trim controls but preserves the saved preset and output preference; it does not erase local data. Device names and configuration can appear in explicitly generated diagnostic output. Test logs and validation artifacts are files created by the developer/test workflows; they are not automatic audio recordings.

If sharing diagnostics or a bug report, review device names, identifiers, local paths, and environment details before publishing them. macOS controls its own capture permission records; Flat EQ does not revoke the operating system’s permission grant. Change that permission in Privacy & Security as needed.

## Boundaries

These statements describe the reviewed source in this repository. A modified or third-party binary may behave differently. A browser or hosting provider can perform its own network requests and access logging when you view a hosted website; the offline site itself does not initiate remote requests.
