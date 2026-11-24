# Windows acceptance checklist — not yet executed

Do not mark the app release-ready until the build succeeds and these scenarios
are exercised on real Windows hardware. A CI build proves compilation and
portable tests only, not that a desktop capture works.

## First smoke test

1. Build Release x64 and run without administrator privileges.
2. Launch Glimpse; confirm one tray icon and no persistent window.
3. Press Ctrl+Shift+R, select a 1280×720 text-editor region and release to start.
4. Move the mouse and scroll for 15 seconds; stop with the global shortcut.
5. Open the MP4 in Windows Media Player. Verify text, cursor, motion, duration,
   last frame, correct orientation/colors, and absence of the toolbar.
6. Paste into Explorer, then try a file-upload destination. Separately test
   dragging from the share panel. A web app rejecting paste is not itself a
   failure if Explorer receives the file and drag/manual upload works.
7. Confirm .partial became .mp4 only after successful finalization, and the
   measured sidecar agrees with the file size and approximate duration.

## State transitions and timing

- Escape during selection; cancel while starting; discard during recording.
- Start then stop immediately: a short playable video or explicit error, no hang.
- Ten rapid record/stop cycles: no repeated tray icons, leaked capture sessions,
  silent overwrites, stale share panel, or stuck hotkey.
- Pause five seconds after start; wait ten seconds; resume for five seconds.
  Output duration should be approximately ten seconds, not twenty.
- Repeated pause/resume and stop while paused.
- Exit during recording/starting/finalizing: finalize first, then quit.
- Lock session: stop and save. Unlock must not silently resume recording.
- Close/kill the app mid-recording: existing completed files remain untouched;
  a partial may remain and is not promised to be playable.

## Displays and controls

- Windows 10 2004+ and Windows 11, SDR, 100%/125%/150%/200% DPI.
- 4K, portrait, negative desktop coordinates, disjoint monitor layouts.
- Selection spanning two monitors, including monitors on separate GPUs.
- Reverse-direction drag and two-pixel minimum regions.
- Drag the toolbar between monitors while recording; verify control/font scaling
  and that its movement does not move the captured region.
- Full-display selection: toolbar remains usable but absent from video.
- Change resolution, move a monitor in Display Settings, disconnect a display.
  Expect stop/error with usable data finalized when possible.
- Remote Desktop, sleep/resume, display-driver reset: report failures without
  silently switching to a different capture surface.
- Keyboard navigation, high contrast, Narrator button labels, light/dark modes.
- Duplicate hotkey collision; change shortcut; restart and verify persistence.
- Restart Explorer and confirm the tray icon is re-added.

## Encoding and output

- All presets, forced software mode, and hardware-enabled initialization.
- Static text, scrolling code, animations, and high-motion video.
- Five-minute and 60-minute sessions; monitor memory for unbounded growth.
- 4K60 may exceed hardware capability: explicit errors or actual lower FPS
  must be documented, not represented as meeting the target.
- Encoder initialization failure should try software input; runtime encoder
  failure should present an error and retain whatever can be recovered.
- Protected content must remain restricted. Do not treat black protected video
  as an invitation to bypass Windows controls.

## Files and sharing

- Unicode folder path; redirected Videos; read-only and nonexistent directories;
  disconnected network share; low disk space before and during capture.
- Existing same-named MP4/partial files: never overwritten.
- Clipboard held by another process for >2 seconds: saved MP4 survives; the
  user sees the retry/drag alternative. Retry Copy file after releasing it.
- Delete a recent capture externally; share attempt should fail gracefully.
- Opt-in startup survives relaunch; disabling removes the Run entry.
- No automatic deletion, uploads or network connections.

Record OS, GPU, driver, build hash, test result, error HRESULT, sidecar metrics
and a representative capture. No rows above are marked passed in this delivery.

## Version 0.2 audio and design checks — pending Windows execution

- Each of Silent/Mic/System/Both: verify intended tracks and no unintended audio.
- Narrate over a playing video; verify speech intelligibility at default mix.
- Independent 0%, 50%, 100% sliders, source mute/unmute, meter response and labels.
- Enable the initially unused source mid-recording; mute both and re-enable.
- Pause while speaking: no paused speech in output, resumed speech stays in sync.
- Long-session AV sync against visible claps or an AV synchronization test video.
- 44.1/48/96 kHz default devices, mono mic, stereo and surround playback; check
  Windows audio-engine format conversion and channel mapping.
- No playback packets while desktop is silent: audio track keeps correct duration.
- Mic privacy denied, no default mic, disconnected mic/output, exclusive-mode use
  (before and during recording): video keeps recording, the source becomes
  silence, one tray message names the cause, and the source resumes within ~2 s
  of being freed (with a "available again" message). Share summary notes the gap.
- Heavy video encoding: inspect audio discontinuity/drop counters for starvation.
- Border/dot/toolbar stay absent from output; dot blinks red only while recording.
- Small regions, full screen, multiple monitors, mixed DPI: toolbar stays usable.
- Toast does not steal focus or disappear during a drag; hover extends timeout.
- Silent sessions show disabled audio controls with an explanation in tooltips.
- Keyboard slider adjustment, focus indicators, screen-reader names, contrast,
  and Windows 10 rounded-corner fallback.
