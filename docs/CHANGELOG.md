# Glimpse 0.3.3

- Glimpse now checks for updates by itself 15 seconds after it starts. If a
  newer version exists, a tray notification offers it (click to install) and the
  tray menu shows **Install Glimpse x.y.z…**. Nothing is shown when you're up to
  date or offline, and no notification appears during a recording.

# Glimpse 0.3.2

- New **Check for updates…** item in the tray menu. If a newer release exists,
  one click installs it: Glimpse downloads the installer, verifies its SHA-256
  checksum against GitHub's, closes, updates silently and reopens. The portable
  zip opens the download page instead. It also tells you when you're up to date
  or the check couldn't connect.

- A microphone or sound output that another app holds in exclusive mode (or
  that is blocked, missing or unplugged) no longer stops the recording. Glimpse
  keeps recording, fills that source with silence, shows a tray message saying
  which source is missing and why, and picks it back up automatically once it's
  free. The saved-file summary says "without microphone" or "mic gaps" when
  that happened.

# Glimpse 0.3.1

- Fixed "Windows could not exclude the recorder controls from capture" right
  after selecting a region. The new recording dot asked Windows to hide it before
  it had drawn anything; it now draws first, and if Windows still refuses, the
  dot is skipped instead of stopping the recording.
- That error message now names the window and the Windows error code.

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
- Frame surfaces are pooled and reused (tracked samples) instead of allocating
  a new GPU texture and view every frame.
- Standard build: CMake presets, version defined once in CMakeLists.txt,
  Inno Setup installer (per user, Start menu, uninstaller), CI on every push and
  a one-click Release workflow. Removed build-windows.cmd and old patch notes.

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
