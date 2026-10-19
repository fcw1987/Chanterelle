# Chanterelle, Mac acceptance checklist

This historical checklist records 1.0.0 build 1, Release candidate 1, and remains a guide for future live hardware validation. Stable 1.0.0 build 3 is released; the historical results below are not new measurements. Native build and off-state checks passed; live hardware acceptance is pending. Keep playback volume comfortable while checking route changes and recovery.

## Completed RC evidence

- Native arm64 compilation and local ad hoc signature verification passed. Installation was not performed.
- All 12 automated RC phases passed; see `Tests/results/rc-final/summary.txt` and detailed logs.
- Off-state native self-test passed without creating audio resources.
- UI smoke geometry passed at 500 × 775 with capture off. Cached PNG content is incomplete; full visual QA remains pending.
- Read-only LG USB stereo hardware baseline: 48 kHz, 512 frames, default/system output, unmuted, volumes approximately 0.19. Output and volume were preserved.

Live permission approval was requested but not received. Permission/capture/playback, listening, latency, physical recovery, sustained playback, installation, and full visual inspection below are **not executed**. Record actual results before accepting personal use.

## Test record

Mac model:

macOS version:

Xcode or Command Line Tools version:

LG display model:

Cable and connection path:

Audio sample rate:

Build date or source revision:

Tester and date:

## Build and installation

1. On macOS 14.2 or later, run `bash build.sh --run`. Confirm successful native compilation, signature verification, installation to the personal Applications folder, and menu bar launch. Record any compiler warnings.
2. Quit, then launch `~/Applications/Chanterelle.app` directly. Confirm a single menu bar instance opens normally.
3. With a copy running, try rebuilding an existing copy. Confirm installation refuses to replace the running app and leaves playback operating normally. Quit, rebuild, and confirm a recognized copy updates successfully.
4. Review the bundle identifier check in a separate disposable test directory if testing collision behavior. Never replace or rename an unrelated installed app just to exercise this check.

Result and notes:

## Permission and first playback

1. Select the LG display as the default macOS output and confirm ordinary audio works before starting the app.
2. Start tuning. Confirm the system audio capture permission request identifies Chanterelle and explains its purpose. Confirm the app does not ask to access the microphone.
3. Deny permission for one test, then confirm a useful error appears and ordinary audio continues. Grant the relevant recording permission in System Settings, quit, reopen, and start again.
4. With the LG selected, start tuning and confirm audible stereo playback. Confirm the original audio is not doubled, echoed, or fed back through the capture path.
5. Try selecting an output that is not the Mac's default. Confirm the app explains the required Sound settings change and leaves ordinary playback working.

Result and notes:

## Tone controls and comparison

1. Choose Neutral. Confirm both channels play, the Signal meter reacts, and there is no obvious dropout or distortion at ordinary listening levels.
2. Adjust bass, mid, and treble individually using familiar music and spoken audio. Confirm the audible changes match the intended frequency regions.
3. Move controls during continuous playback. Confirm transitions do not cause clicks, loud bursts, or interruptions.
4. Toggle Bypass tone controls repeatedly. Confirm tone changes are removed, output trim and EQ headroom reduction remain, and transitions are smooth.
5. Lower output trim and confirm overall volume falls. Check the documented control limits and the app's overload indication if one is shown.
6. Stop and start tuning several times. Confirm ordinary audio resumes when stopped and the current settings are applied again after starting.
7. Adjust a preset, choose Save my preset, then quit and reopen the app. Confirm the tone controls and My preset are restored while tuning remains off. Confirm bypass starts unchecked.

Result and notes:

## Route changes and recovery

1. While audio is playing through active tuning, choose headphones or another output in macOS Sound settings. Confirm tuning stops, the new device plays normally, and no stale LG route remains active.
2. Return macOS output to the LG, select it in the app if needed, and start tuning again. Confirm clean playback resumes.
3. Disconnect the LG while tuning is active. Confirm the app stops and shows a useful status. Reconnect it, reselect the output, and confirm manual restart works.
4. Change the output sample rate in Audio MIDI Setup while tuning is active, where the device supports it. Confirm the app stops instead of processing stale audio formats. Restore the desired rate and restart.
5. Put the Mac to sleep while tuning is active, then wake it. Confirm ordinary audio is available after waking and that tuning can be restarted as needed.
6. Quit Chanterelle while music is playing. Confirm the original audio route resumes and the default output selection and hardware volume remain as they were.
7. In Activity Monitor, force quit Chanterelle during playback. Confirm macOS releases the process tap and ordinary audio returns. Record any delay or manual recovery needed. This check specifically validates process death behavior, which cannot be established by reviewing the normal stop path.
8. After stopping and quitting, inspect Audio MIDI Setup. Confirm no private Chanterelle aggregate device remains available as a stale output.
9. Leave normal music or spoken audio running through tuning for at least 15 minutes, including a few controls changes. Record dropouts, distortion, latency that affects video, unexpected CPU use, or memory growth.

Result and notes:

## Acceptance decision

Native build: PASS (arm64 local build); installation pending

Permission and capture:

Sound and controls:

Stop and quit recovery:

Force quit recovery:

Device change and sleep recovery:

Remaining issues:

Accepted for personal use, yes or no: PENDING live acceptance
