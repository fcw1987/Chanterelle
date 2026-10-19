# RC validation

Run `Tests/run_rc_tests.sh` from any directory. It runs the optimized portable
EQ/ring tests, AddressSanitizer plus UndefinedBehaviorSanitizer, ThreadSanitizer,
and all three variants of the actual-source callback tests. On macOS it also
runs the extracted cleanup method against fake audio resources under normal
compilation and AddressSanitizer plus UndefinedBehaviorSanitizer, and verifies
installer refusal for running, legacy, unrecognized, or symlink app paths
using temporary fixtures. Every phase
gets its own log; any failed phase makes the overall runner fail. Unsupported
sanitizers are reported as failures, not silently treated as passing.

The runner also measures a synthetic EQ plus ring workload at 48 kHz using 64,
256 and 1024 frame blocks with tone processing enabled and bypassed. The log
includes process CPU time divided by equivalent audio duration, block timing
percentiles and maximum, and clip/underrun/overrun counts. Scheduler interruptions
can affect wall-time maxima. These results do not measure device latency or the
complete app's CPU use and do not establish realtime scheduling guarantees.

Results default to `Tests/results/<UTC timestamp>/`. Set `CH_RC_RESULTS_DIR` to
choose another directory. Sanitizer leak detection defaults to disabled; set
`ASAN_OPTIONS` to override. `CC` selects the compiler. Python 3 is required for
the extraction fixtures.

After a native app build, include its off-state checks by passing the executable:

```sh
CH_RC_APP_BINARY="$PWD/dist/Chanterelle.app/Contents/MacOS/Chanterelle" \
    Tests/run_rc_tests.sh
```

This invokes only `--self-test` and `--diagnostics`. It does not install or open
the UI, start capture or playback, request capture permission, change a device,
or modify the user's saved controls.

The 16 EQ groups include exact clamp boundaries and per-channel clip counts,
bypass fidelity with trim and headroom at six rates, and return from bypass
against continuously maintained wet filter history. The extreme finite-input
regression detects contraction of matched numerator/denominator products that
previously left a huge clipped tail on ARM after a flat ±FLT_MAX input.

Nine callback groups extract the current C callback section from the engine.
They cover waiting and active output, layouts and bounds, missing input's
immediate render gate, underrun readiness and rebuffering, stopped callbacks,
and simultaneous producer/consumer callback execution.

The cleanup fixture extracts the current `finishWithError:notify:` method and
supplies Foundation objects plus fake handles and HAL functions. It checks all
256 combinations of eight cleanup operation failures, retained inactive callback
contexts, retry and idempotent cleanup, already-destroyed objects, preservation
of an original error, and ten partial-start allocation checkpoints. No Core
Audio framework is linked. The fixtures validate our resource bookkeeping;
they cannot establish Apple's callback quiescence, process-death behavior,
permission handling, or sound quality. Those require the Mac acceptance checks.

RC2 also runs the shared preset table through `Tests/test_presets.c` in the portable runner: finite gain/trim bounds, unique names, flat versus nonflat headroom, and retained headroom during comparison for all eight presets. UI smoke checks are separate and never start capture.
