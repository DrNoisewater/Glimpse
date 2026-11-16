# Performance and validation report

## What was actually tested

Authoring host: Linux, GCC 14. No Windows SDK, MSVC, Windows runtime, or access
to the user's display/GPU was available. Windows compilation was not performed.

The portable C++20 test program passed **341 checks** covering negative desktop
coordinates, reversed selection, per-monitor intersections, portrait and
ultrawide output sizing, no upscaling, even encoder dimensions, preset FPS and
bitrate limits, invalid selections, and pause/resume timing.

Executed command:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I src tests/policy_tests.cpp -o /tmp/glimpse-policy-tests
ASAN_OPTIONS=detect_leaks=0 /tmp/glimpse-policy-tests
```

Address/undefined-behavior sanitizers passed. LeakSanitizer was disabled because
the sandbox's ptrace environment prevents it from running. These tests exercise
portable policy code only, not Win32, D3D, WGC, WASAPI or Media Foundation.

| Required measurement | Result in this delivery |
|---|---|
| Windows Release EXE size | Not measured — no Windows compilation |
| Idle working set/private bytes | Not measured |
| Idle CPU | Not measured |
| Capture-start latency | Not measured on Windows |
| Recording CPU/GPU | Not measured |
| Encoder throughput and sustained FPS | Not measured |
| Stop/finalize latency | Not measured |
| Example MP4 sizes and text quality | Not measured — no sample captures produced |
| Windows 10/11 end-to-end correctness | Not tested |

No speed, RAM, visual-quality, hardware-encoding or reliability target is claimed.

## Collect real measurements on Windows

Build and launch the app, then run:

```powershell
powershell -NoProfile -File scripts\Measure-Glimpse.ps1 -Seconds 30 -Output idle.csv
```

Repeat while recording the same 30-second workload with each preset, using a
different output filename. The script measures private bytes, working set,
total CPU time delta, and process CPU as a percentage of the machine's logical
processors. GPU-engine counters are collected when supported; these may be
unavailable or localized. Capture sidecars record first-ready-frame latency,
video duration, frame submissions, missed scheduling intervals, file bytes,
GPU-input use and whether quality VBR was accepted.

Start latency begins when the recording worker enters initialization and ends
when every participating monitor has delivered its first frame. It excludes
selection time, thread-launch overhead, and the first encoded-sample submission.
Use a screen-visible stopwatch for independent end-to-end timing. Late intervals
measure missed recorder scheduling slots, not WGC's internal dropped-frame count.

Compare a static text editor, fast scrolling, window dragging, and high-motion
content. Inspect resulting text at native pixel size. For accurate comparisons,
use identical region, display scaling, source content and duration. In Task
Manager, inspect the process's **Video Encode** engine to help verify hardware
encoding; GPU-input mode alone is insufficient evidence.

After a Windows build, add measured values here together with OS build, CPU,
GPU, driver, display layout/DPI, preset, sample duration and source workload.

## Version 0.2 validation

The original 341 policy checks and 10,111 audio-policy checks passed under GCC
with address/undefined-behavior sanitizers (LeakSanitizer disabled as above).
Audio checks include channel mixing, volume scaling, saturation, missing/late
packets, bounded-ring reuse, 48 kHz timestamp quantization, pause-resume offsets,
and one-hour clock arithmetic. The repeated quantization checks exercise 10,000
sample positions; they are not 10,000 distinct end-to-end scenarios.

Windows compilation, actual WASAPI input, AAC output, AV sync, UI screenshots,
device compatibility, recording performance and file-size measurements remain
unverified. No compiled Windows EXE is included.
