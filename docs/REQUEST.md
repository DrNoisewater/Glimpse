# Build a Native Windows Instant Screen Recorder

## The Vision

Create an extremely lightweight Windows 10/11 application that combines the simplicity of Windows Snipping Tool with fast, high-quality screen video recording and effortless sharing.

The app should feel like a feature built directly into Windows, not a third-party screen recording suite.

**The primary goal: Select an area → Record → Stop → Immediately share the compressed video.**

I frequently need to record bugs, software behavior, UI interactions, or short demonstrations to send to AI assistants such as Claude and ChatGPT, as well as Discord, GitHub, email, and other applications.

I do not want to launch OBS, configure a recording session, open an editor, or manually compress videos.

The entire process should take as few actions as possible.

This is a functional application request. Build the software, not just mockups or a development plan.

## 1. Architecture — Extremely Lightweight

Prioritize execution speed, low RAM consumption, low CPU overhead, and minimal installation size.

Preferred technical direction:

- Native C++20 with Win32 APIs.
- Windows.Graphics.Capture for hardware-accelerated desktop capture.
- Direct3D 11 for efficient frame processing and cropping.
- Windows Media Foundation for MP4/H.264 encoding.
- Prefer hardware encoding through Intel Quick Sync, NVIDIA NVENC, or AMD hardware where supported.
- Provide a reliable software encoding fallback.
- Minimal external dependencies.
- No Electron, Chromium, embedded browser, web server, or constantly running heavyweight framework.
- No cloud processing, telemetry, accounts, or subscriptions.

Use another native implementation only if it provides demonstrably better reliability or performance.

The application should run quietly in the system tray without needing a permanent window.

## 2. Snipping Tool-Style Workflow

A configurable global hotkey, initially Ctrl+Shift+R, activates the recorder.

When activated:

1. The screen dims slightly.
2. The pointer becomes a crosshair.
3. The user clicks and drags to select any rectangular area.
4. The selected region remains bright while the remainder is dimmed.
5. Display a tiny floating recording toolbar outside the capture region.
6. The user starts recording with one click or Enter.
7. Pressing the recording hotkey again stops and saves.
8. The video becomes available immediately.

Escape cancels selection.

Support all monitors, different resolutions, mixed DPI scaling, and negative desktop coordinates.

Do not require dragging the application window onto the desired monitor.

Optionally allow selecting a full monitor or individual window, but make rectangular selection the primary experience.

## 3. Fast, High-Quality Video

This is the most important technical requirement.

The recording must preserve readable UI text, mouse movement, menus, animations, and application details while maintaining a small file size.

Provide three simple quality presets:

**AI Share — Default**
- Optimized for short software demonstrations.
- MP4 using H.264 for broad compatibility.
- 30 FPS by default.
- Preserve source resolution where practical, with an intelligent 1080p output ceiling.
- Adaptive bitrate or quality-controlled encoding.
- Favor legible text and smooth motion over unnecessary visual detail.
- Minimize files without introducing noticeable blocking or blurred text.

**High Quality**
- Up to 60 FPS.
- Higher resolution and bitrate.
- Suitable for animation, software previews, and gameplay.

**Smallest File**
- Prioritize minimum storage size.
- Dynamically reduce output resolution, frame rate, or bitrate as appropriate.
- Intended for quick sharing and uploads.

Avoid fixed, excessively high bitrates that waste storage on static desktop content.

Support recordings with little motion efficiently.

Use the GPU for capture, scaling, and color conversion where available. Avoid unnecessary CPU copies of every frame.

Handle encoder initialization, dropped frames, variable frame timing, resizing, and finalization reliably.

A recording must remain playable after stopping.

## 4. One-Action Sharing

This is what separates the app from a conventional recorder.

After stopping:

- Automatically save the MP4 to a configurable capture folder.
- Automatically copy the resulting video **file reference** to the Windows clipboard in a format compatible with file pasting.
- Show a small, nonintrusive notification with the filename, file size, and duration.
- Provide Copy File, Open Folder, and Drag to Share actions.
- Allow pasting into applications that support video-file clipboard uploads.
- If a destination does not accept pasted video files, make drag-and-drop or manual upload straightforward.

Do not pretend that ChatGPT, Claude, or every web application supports identical clipboard behavior.

No mandatory account integrations, API keys, browser extensions, or cloud uploads.

The app should work naturally alongside these tools without depending on them.

## 5. Minimal Recording Toolbar

The floating toolbar should be exceptionally compact and polished.

Controls:
- Record / Stop.
- Pause / Resume.
- Microphone toggle.
- System audio toggle, if implemented.
- Elapsed recording time.
- Cancel.

Do not display unnecessary settings during recording.

Keep the toolbar outside the recorded area where possible and exclude the app's own overlay from capture.

Allow moving it without changing the selected recording region.

The recording toolbar should be subtle, with excellent contrast, readable icons, restrained animation, and proper scaling.

## 6. Audio

Audio should be optional and disabled by default to keep software bug recordings small.

Offer:
- Silent recording (default).
- Microphone.
- System audio.
- Microphone and system audio together.

Use native Windows audio APIs and suitable audio compression.

If audio is more complicated than the base recorder, implement the stable video-only recording path first and then add audio as a separate, testable module.

Do not sacrifice capture reliability to add audio functionality.

## 7. User Experience

The application should have almost no visible interface until needed.

Design language:
- Modern, clean, professional Windows 11 aesthetics.
- Native-feeling window shadows and rounded corners where supported.
- Compact, readable typography.
- Subtle dark and light theme support.
- Minimal animations.
- Proper Windows 10 fallback.
- No cluttered dashboard or exaggerated futuristic design.
- No unnecessary welcome screens or setup wizards.

The tray menu should contain:
- New Recording
- Recent Captures
- Open Captures Folder
- Settings
- Exit

Settings should be a single compact window containing the hotkey, storage folder, quality preset, frame-rate preference, audio options, and startup behavior.

Do not introduce an editor, timeline, project system, media library database, or account management.

## 8. Instant Capture and Performance

Target an application that feels virtually instantaneous.

Performance priorities:
- Extremely low idle CPU usage.
- Minimal idle RAM.
- Fast hotkey response.
- Very short delay between selection and recording.
- Hardware-accelerated encoding when available.
- Asynchronous saving and finalization without freezing the UI.
- No unnecessary background activity.
- No recording session initialized until actually required.

Measure actual capture-start latency, idle RAM, recording CPU/GPU load, encoding performance, and resulting file sizes.

Do not claim performance targets are met without testing them.

## 9. Intelligent File Management

Each capture should be saved automatically using a predictable filename, such as:

Capture_2026-11-15_143025.mp4

Additional functionality:
- Configurable destination folder.
- Recently recorded files accessible from the tray.
- Optional automatic cleanup based on age or total storage.
- Never delete recordings without explicit user opt-in.
- Graceful disk-space error handling.
- Never silently overwrite an existing file.
- Recover cleanly from interrupted or failed recordings wherever feasible.

Keep files private and local.

## 10. Optional Screenshot Function

Because the application already uses a Snipping Tool-style selection overlay, consider adding still-image capture through a separate hotkey.

Screenshot behavior:
- Select region.
- Capture PNG.
- Copy image to clipboard.
- Optionally save to the captures folder.

However, video capture is the primary feature. Do not complicate the architecture or UI to support screenshots.

## 11. Reliability and Edge Cases

Test:
- Windows 10 and Windows 11.
- Multiple monitors.
- 4K and high-DPI displays.
- Different display scaling percentages.
- Selection spanning displays where technically supported.
- Rapidly starting and stopping recordings.
- Extremely short recordings.
- Extended recordings.
- High-motion software and relatively static desktop content.
- Changing resolution or disconnecting a monitor.
- Lost capture devices.
- Unavailable hardware encoders.
- Canceling recordings.
- Clipboard failures.
- Invalid save paths.
- Running without administrator privileges.

Respect Windows capture permissions and protected-content restrictions. Do not bypass security indicators or protected-content controls.

Recording failures should present understandable errors, not crashes.

## 12. Deliverables

Produce:
1. The complete working source code.
2. A reproducible Windows build configuration.
3. A Release x64 executable.
4. A portable build, preferably without a separate installer.
5. An optional lightweight installer for startup and shortcuts.
6. A short README documenting hotkeys and usage.
7. A brief performance report showing RAM, CPU, capture latency, and example video sizes.

Provide the real compiled output where the development environment permits it.

Do not substitute mockups or placeholder recording functionality for implementation.

## 13. Implementation Order

Build incrementally, testing each milestone:

**Phase 1:** System tray, global hotkey, monitor-aware region-selection overlay.

**Phase 2:** Functional screen capture, GPU encoding, and playable MP4 output.

**Phase 3:** Fast Stop → Save → Copy workflow and drag-and-drop sharing.

**Phase 4:** Compact toolbar, settings, presets, and native Windows styling.

**Phase 5:** Optional audio, performance optimization, and comprehensive testing.

Do not begin adding secondary features until recording and saving work reliably.

## 14. Final Quality Standard

The final app should feel so integrated into Windows that I forget it is a separate application.

I should be able to spot a bug in software I am developing, press one hotkey, drag over the problem, record 15 seconds, stop, and immediately attach that video to a ChatGPT or Claude conversation with minimal friction.

**Judge every design and technical decision against three priorities:**

1. Speed — absolutely minimal interaction and processing delay.
2. Quality per byte — videos must look clear without wasting storage.
3. Simplicity — the application should require almost no learning.

If a feature does not meaningfully improve one of those priorities, leave it out.

Implement the smallest complete, reliable product that achieves this experience.