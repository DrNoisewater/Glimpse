# Glimpse 0.3.0

- Selection overlay rewritten: frozen, dimmed desktop with the selected area
  shown at full brightness, an accent outline with corner handles and a size
  label. Double-buffered and only changed areas are repainted, so the old
  flicker (full-screen layered window + region rebuilt on every mouse move) is gone.
- Click a window or the desktop to record that window or whole screen; Shift
  while dragging for a square; right-click cancels a drag.
- New compact recording toolbar with icon buttons (Pause/Resume, Stop, Discard),
  hover states, smooth 20 Hz level meters and a pulsing status dot. The audio row
  only appears when audio is enabled.
- Anti-aliased pulsing recording dot; region border no longer repaints every 250 ms.
- Refreshed save toast, settings (dark-mode fields), tray menu shows the shortcut.
- New app/tray icon (smooth edges, sizes 16–256).
- `.mp4.metrics.txt` diagnostics sidecar is now opt-in (Settings), off by default.
- H.264 High profile with Main fallback and a 4-second keyframe interval for smaller files.

# Glimpse 0.2.0 — source update

- Drag-release starts recording automatically; no confirmation click.
- Capture-excluded border and blinking red top-right status dot.
- Top-right recording panel with distinct recording, paused and saving states.
- WASAPI microphone narration, system-audio loopback, or both, with AAC muxing.
- Independent volume sliders, live signal meters and per-source mute buttons.
- Persistent audio preferences and quick tray audio selection before recording.
- Rounded dark/light surfaces, updated typography, keyboard-accessible custom
  sliders, tooltips, and a dismissible save toast with file-sharing actions.
- Portable capture-policy and audio-mixer tests passed. Windows build, visual
  validation, real audio capture, AV synchronization and performance remain
  pending. No Windows executable is included in this source archive.
