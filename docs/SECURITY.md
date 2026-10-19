# Security and distribution

Version 1.1.0 (build 5) is Developer ID signed and Apple notarized. The release app uses hardened runtime, has a secure signing timestamp, and includes a validated stapled notarization ticket. The exact packaged app passes strict signature verification and Gatekeeper assessment. Default source builds receive an ad hoc signature; that signature is a bundle integrity check and does not identify a trusted publisher.

The native runtime depends on Apple AppKit, Foundation, Core Audio, AudioToolbox, and AudioUnit frameworks plus the in-tree DSP and ring buffer. It has no package-manager runtime dependencies, network update mechanism, or automatic startup. Tuning starts manually. The app does not change hardware volume or the default output device.

The engine bounds its stereo buffer and validates supported layouts. Tone boosts reserve headroom and output samples are clamped. Automated sanitizer and lifecycle fixtures cover a range of source-level errors. They do not prove realtime guarantees, all input behavior, live capture permission handling, or physical recovery. See [known limits](KNOWN_LIMITS.md).

## Reporting a problem

Use GitHub [private vulnerability reporting](https://github.com/fcw1987/Chanterelle/security/advisories/new) for sensitive security reports. Do not publish audio, personal device identifiers, credentials, or exploit details in a public issue.

For a non-sensitive bug, provide the source revision, macOS/toolchain version, output model and connection, sample rate, exact reproduction steps, observed status, and expected result. Review diagnostic files for local paths and identifiers before sharing. No personal contact details are included in this project.

## Distribution status

Stable 1.1.0 (build 5) is available for Apple silicon and macOS 14.2+. Its release ZIP and SHA-256 checksum are on GitHub Releases. The 1.0.0 release remains available with its original ad hoc signing status. Distribution verification and signed off-state checks do not establish live capture compatibility under hardened runtime. Live permission, listening, latency, sustained playback and hardware recovery have the boundaries documented in [known limits](KNOWN_LIMITS.md); stable designation does not imply those measurements have been performed.
