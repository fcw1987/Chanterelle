# Use Chanterelle

## Start

Select the LG display speakers as your default output in macOS Sound Settings, then select the same output in the app. Start tuning manually and allow system audio capture when macOS requests it. If permission was denied, enable the relevant audio recording permission in **System Settings → Privacy & Security**, then quit and reopen the app. The exact settings label can vary by macOS version.

Original playback remains audible while the engine waits for a working capture signal. **Waiting for audio** becomes **Tuning your audio** when capture and playback are ready. Extended silence can return the engine to waiting until audio resumes.

## Controls

| Control | Behavior |
| --- | --- |
| Bass | Low shelf near 150 Hz; −6 to +6 dB |
| Mids | Broad band near 900 Hz; −6 to +6 dB |
| Treble | High shelf near 4.5 kHz; −6 to +6 dB |
| Output trim | Overall reduction from −12 to 0 dB |
| Compare original tone | Removes tone adjustment, retaining trim and the selected settings’ reserved headroom |
| Stop tuning | Stops processing and restores ordinary macOS playback |
| Flat EQ | Sets all tone gains and trim to zero, clears comparison, and preserves the saved preset, output, and current processing state |

Presets are **Neutral**, **Everyday**, **Podcast / Speech**, **Gaming**, **Music**, **Clearer voices**, **Less boom**, and **Softer treble**. Gaming changes tone only; it does not add positional enhancement. **Save my preset** stores one custom setting. Tone values and the output preference persist locally; tuning always launches off and bypass starts unchecked.

The shared preset definitions use these gains in dB:

| Preset | Bass | Mids | Treble | Trim |
| --- | ---: | ---: | ---: | ---: |
| Neutral | 0 | 0 | 0 | 0 |
| Everyday | −1 | +0.5 | −0.5 | 0 |
| Podcast / Speech | −2.5 | +1.5 | +0.5 | 0 |
| Gaming | −1 | +1 | +1 | −1 |
| Music | +1 | −0.5 | +0.5 | −1 |
| Clearer voices | −3 | +1 | +1 | 0 |
| Less boom | −4 | 0 | +1 | 0 |
| Softer treble | 0 | −1 | −3 | 0 |

Start at a comfortable playback volume. For boomy sound, try lowering bass by roughly 2 dB. For boxy voices, try lowering mids slightly. For sharp sound, lower treble. These are experiments by ear, not measured correction curves.

Boosts automatically reserve headroom by reducing overall gain. A final sample clamp bounds digital output and counts clipping. It cannot guarantee distortion-free output for every input. Reduce boosts or lower Output trim if peaks reach the ceiling or playback sounds distorted. Comparison keeps the trim and headroom to make tonal comparison less abrupt; use Stop for the ordinary audio path.

## Daily use and recovery

Closing the controls leaves the menu bar app running. Click its icon to open the persistent menu: **Open Window**, status, Start/Stop tuning, **Compare original tone**, **Flat EQ**, listening presets, and Quit. Open Window also reopens a closed controls window. Output changes, disconnects, device configuration changes, and sleep stop tuning. After waking or reconnecting, confirm the current route and restart manually.

| Symptom | Next step |
| --- | --- |
| Start is unavailable | Select a supported stereo output that is already the Mac’s default in Sound Settings. |
| Waiting for audio persists | Play familiar audio; check capture permission; stop and reopen after changing permission. |
| Silence or unexpected playback | Stop tuning first, confirm ordinary playback and the chosen output, then retry. |
| Sound distorts | Reduce tone boosts and lower Output trim; check the input content. |
| Tuning stopped after a route change | Confirm the desired macOS output and restart manually. |
| Video feels out of sync | Stop tuning to compare. Physical latency remains unmeasured. |
| Some content is not processed | Protected content or an app’s direct output routing may not be captured. |

If ordinary playback does not recover after Stop or Quit, record the macOS version, output model, route, sample rate, status/error text, and reproduction steps in the acceptance checklist. Force-quit recovery is a required live acceptance test and remains unverified in the recorded RC evidence.
