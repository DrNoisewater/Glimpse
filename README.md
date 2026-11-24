# Glimpse 0.3.0

A small, local Windows region recorder for Noisewater Labs. Native C++20 / Win32,
Windows.Graphics.Capture, Direct3D 11 and Media Foundation. No browser runtime,
FFmpeg download, account, server, telemetry or upload integration.

## Install

Download **Glimpse-Setup-x.y.z.exe** from the
[Releases](https://github.com/DrNoisewater/Glimpse/releases) page and run it.
It installs per user (no administrator prompt) to
`%LOCALAPPDATA%\Programs\Glimpse`, adds a Start menu entry and an uninstaller,
and updates an existing install in place. A portable zip is published next to
it if you'd rather not install anything.

## Build from source

Requirements: Windows 10/11, Visual Studio 2022 or newer with **Desktop
development with C++** (includes CMake and a Windows 10/11 SDK, which provides
C++/WinRT). No other dependencies.

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Or open the folder in Visual Studio and pick the **release** preset. The
program is `build\Release\Glimpse.exe`. It statically links the C++ runtime, so
it runs on a clean Windows install.

## Releasing

The version lives in one place: `project(Glimpse VERSION x.y.z)` in
`CMakeLists.txt`. It flows into the EXE's file properties, the app (Settings
title, tray menu) and the installer.

1. Change the version in `CMakeLists.txt` and commit it.
2. On GitHub: **Actions → Release → Run workflow**, enter the same version.
3. The workflow builds on Windows, runs the tests, builds
   `Glimpse-Setup-x.y.z.exe` (Inno Setup, `installer/Glimpse.iss`) and
   `Glimpse-x.y.z-portable.zip`, tags `vx.y.z` and publishes the release.
   Nothing is tagged if any step fails.

Every push also runs **CI**, which builds, tests and builds the installer; the
files are attached to each run under *Artifacts*.

## Use

1. Launch Glimpse. It appears in the system tray, possibly inside the hidden
   icons menu. There is no persistent main window.
2. Choose **Audio for next recording** in the tray menu if you want narration or system sound.
3. Press **Ctrl+Shift+R**. The screen freezes and dims, like Snipping Tool.
   - **Drag** a rectangle and release: recording starts automatically. Hold
     **Shift** while dragging for a square. Right-click cancels the drag.
   - Or **click** a window (it highlights as you hover) or an empty part of the
     desktop to record that whole window or screen.
4. Press the shortcut again or click **Stop** to finalize the MP4.
5. Paste into a destination that accepts file clipboard uploads, or hold and
   drag **Drag to share** from the saved-capture panel. **Open folder** selects
   the file in Explorer.

A pulsing red dot appears at the selected area’s top-right corner and a thin
border marks the recording region. A compact toolbar (status and timer, Pause,
Stop, Discard, plus mute toggles, volume sliders and level meters when audio is
on) sits just above or below the region and can be dragged anywhere. Paused is
amber; starting/saving is violet. All of these are excluded from the recording.

**Pause / Resume** excludes paused time. **Discard** discards the current
recording; **Escape** cancels selection. Closing the recording toolbar saves
an active recording. Exiting from the tray also saves first. A display-layout
change, device error, or session lock stops capture and attempts to save what
has already been encoded.

Right-click the tray icon for New Recording, Recent Captures (last ten), Open
Captures Folder, Settings, Check for updates, or Exit. Clicking a recent capture opens its share
panel. **Check for updates** asks GitHub for the latest release (sending only a
"Glimpse/version" user agent). If it's newer, click **Yes** to install it: Glimpse
downloads the installer, checks it against the SHA-256 checksum GitHub publishes
for it, closes, updates silently and reopens, keeping your settings. If a recording
is running, the update waits and the menu shows **Install update** for afterwards. The
portable zip can't update itself, so it opens the download page instead. Settings offers the shortcut, folder, quality, frame-rate ceiling,
opt-in sign-in startup, audio defaults and software-encoding compatibility mode.

## Narration, mixing and volume

Choose **Silent**, **Microphone narration**, **System audio**, or **Microphone +
system audio** in Settings or the tray’s **Audio for next recording** submenu.
Silent remains the default. For talking over a video playing on your desktop,
choose **Microphone + system audio**, start that video, then select the region.
The narration and playback audio are mixed into the resulting MP4.

- Capture uses your Windows default microphone and default playback device.
- Separate **Mic** and **Sound** buttons mute/unmute each source during an
  audio-enabled recording. Separate sliders adjust 0–100% volume; labels show
  exact percentages. Their thin meters show current post-volume signal levels.
- Default microphone volume is 100%; default system volume is 65% so playback
  does not dominate narration. Set persistent defaults in Settings. Live changes
  apply to the current recording; they do not change Windows device volume.
- An audio-enabled session can switch either source on/off. Silent sessions
  deliberately contain no audio track; their audio controls are disabled. Choose
  an audio mode **before** dragging to enable narration.
- Output is stereo 48 kHz AAC at 128 kbps. Audio and video use the same QPC clock;
  paused intervals are removed. Missing loopback packets become silence.
- Use headphones when recording microphone plus speaker playback to avoid
  acoustic echo. Echo cancellation and noise suppression are not implemented.
- A soft limiter smooths peaks when both sources are loud, but heavy overload
  can still distort; reduce levels if the meters reach amber/red.
- Muting releases that endpoint. Pause discards audio rather than saving it;
  an enabled microphone may still show Windows’ microphone-use indicator.
- If an audio source can't be opened or drops out mid-recording, the recording
  keeps going: that source is recorded as silence, Glimpse shows a tray message
  saying why, retries every 2 seconds, and resumes it automatically once it's free.
  The saved-file summary notes "without microphone" or "mic gaps" when that happened.
- Apps that take **exclusive control** of a device (some music and live-audio
  apps do this for low latency) lock everyone else out; Windows doesn't let any
  other app read that device until they let go. To record it alongside such an
  app, set that app to shared mode, or in Windows open Sound settings → the
  device → Properties → Advanced and untick *Allow applications to take
  exclusive control of this device*. A different microphone also works if the app
  only holds one device. Device hot-swapping and a dedicated device picker are
  not implemented; choose defaults in Windows.
- This records narration **while capturing**. It is not an editor for adding
  voiceover to an existing video file afterward.

## Save feedback

Stopping shows **Saving…** until MP4 finalization completes. The rounded native
save toast then shows the filename, size, duration, audio status and clipboard
result, with **Copy file**, **Open folder** and **Drag to share** actions.
It does not steal focus. It dismisses after 12 seconds of inactivity, pauses
that countdown while hovered/focused/dragged, and has a close button. Files remain
accessible under Recent Captures. This is an in-app toast, not an Action Center
notification requiring packaged-app identity.

## Presets

Video is H.264 **High** profile (falls back to Main if an encoder refuses it)
with a keyframe every 4 seconds, which suits mostly-static screen content and
keeps files small.

| Preset | Output ceiling, landscape / portrait | FPS ceiling | Requested codec quality |
|---|---|---:|---:|
| AI Share | 1920×1080 / 1080×1920 | 30 | 78 |
| High Quality | 3840×2160 / 2160×3840 | 60 | 90 |
| Smallest File | 1280×720 / 720×1280 | 15 | 60 |

Source aspect ratio is retained within even-pixel rounding; small selections
are never upscaled. These are ceilings, not promised sustained frame rates.
Quality-based VBR is requested through the encoder's `ICodecAPI`; when the
encoder does not accept it, the output media type supplies a resolution- and
frame-rate-scaled bitrate. This is not a guaranteed file-size target. No
runtime scene classifier or dynamic resolution changes are implemented.

## Local files and privacy

- Default destination: `%USERPROFILE%\Videos\Glimpse` (the Windows Videos
  known folder is respected if redirected).
- Names: `Capture_YYYY-MM-DD_HHMMSS_mmm.mp4`, with a numeric suffix on collisions.
- Settings: `%LOCALAPPDATA%\Glimpse\settings.ini`, UTF-16, replaced atomically.
- An optional sign-in entry is written only to the current user's Run key.
  No administrator privileges are requested. The uninstaller removes it; if you
  use the portable zip, turn the setting off before deleting or moving the EXE.
- Only the `.mp4` is saved by default. Turning on **Save a diagnostics file**
  in Settings also writes a `.mp4.metrics.txt` sidecar with timing and audio
  counters. It is for troubleshooting only, and nothing needs it to play or share the video.
- Capture writes to `.mp4.partial`, then finalizes and renames without replacing
  an existing video. The app never automatically cleans up older recordings.
- Failed finalization retains the partial file and shows its path. Some partial
  MP4s are not playable; crash/power-loss recovery is **not guaranteed**.
- Clipboard transfer is `CF_HDROP`, a file reference. Web applications differ
  in what they accept. There is no claim of universal ChatGPT/Claude paste support.
- Capture indicators remain under Windows control. Protected content is not
  bypassed. Controls use Windows capture exclusion; capture is refused if that
  exclusion cannot be set.

## Scope and current limitations

- Windows 10 **2004/build 19041+** or Windows 11. Older Windows 10 builds are
  deliberately rejected because reliable overlay exclusion is required.
- Microphone and system audio are implemented in this version. Screenshot mode,
  automatic cleanup, installer and post-recording editing are not implemented.
- Spanning displays is implemented by composing intersecting monitor textures
  on one D3D device. Monitor gaps are black. Hybrid/multiple-GPU machines,
  rotated screens and mixed DPI require hardware testing.
- SDR BGRA capture / BT.709 NV12 output. HDR tone mapping is not implemented;
  disable HDR for color-critical recordings.
- Hardware transforms are enabled first. Initialization failure retries with
  software encoding and a CPU readback buffer. GPU color conversion is still
  required in compatibility mode. Machines with no usable video processor
  receive an error; WARP video-processing support is not assumed.
- A GPU-input diagnostic does **not** prove NVENC/Quick Sync/AMD hardware
  encoding was chosen. Windows and its driver choose the transform. If the
  encoder fails after initialization, the app saves what it can and offers a
  readable error; choose software mode before retrying.
- NV12 frame surfaces come from a small pool. A surface is reused only after
  Media Foundation releases the sample holding it (tracked samples), so an
  asynchronous encoder never sees a frame overwritten.
- Theme (light/dark) is read at launch. Panels use anti-aliased GDI+ drawing,
  Segoe Fluent Icons / Segoe MDL2 Assets glyphs, native rounded corners on
  Windows 11 and a region fallback on Windows 10. The hotkey control keeps
  Windows styling.

See `docs/TESTING.md`, `docs/PERFORMANCE.md` and `docs/ARCHITECTURE.md`.

### Microphone and system audio crackle

The audio capture path now uses **two dedicated WASAPI worker threads**, one for the
Windows default microphone and one for default playback loopback. This prevents
blocking H.264 encoding, GPU conversion, or MP4 I/O on the screen-recording
thread from starving both audio capture buffers. The audio samples still mix
on a synchronized timestamped 48-kHz stereo ring, with a final 300-ms output
staging margin.

### Troubleshooting crackle or pops in recorded microphone audio

Glimpse captures the **Windows default recording microphone** through WASAPI.
For a FIFINE AM8 connected by USB, verify in Windows **Sound > Input** that
FIFINE AM8 is the default device (not an unused headset mic). If it is available,
use a 48 kHz recording format in **More sound settings > Recording > Properties > Advanced**.
This matches Glimpse's 48 kHz AAC output and can avoid extra resampling.

Make a short **microphone only** recording and a separate **system audio only**
recording before testing the mixed mode. A USB mic that sounds fine in OBS
should not require buying new audio hardware to work with Glimpse. Keep the mic
gain below 100% if the recorded audio reaches full scale during speech.

Turn on **Save a diagnostics file** in Settings, record again, then open
`Capture_...mp4.metrics.txt` beside the video:

- `audio_dropped_frames`: audio packets fell outside the staging window,
  often because they arrived after the MP4 writing deadline; nonzero values
  during speech can sound like gaps or clicks.
- `audio_discontinuities`: Windows reported lost/overrun audio packets.
- `audio_bad_timestamps` and `audio_inferred_timestamps`: invalid packet timing
  was detected and repaired when continuity was available.
- `audio_timestamp_corrections`: small packet timestamp jitter was smoothed.
- `audio_mixed_overload_frames`: raw microphone + system samples exceeded 0 dBFS
  and were softly limited; reduce a source gain if this is frequent.

Glimpse now leaves a 300 ms audio staging cushion, repairs small packet boundary
jitter, and soft-limits peaks in the combined audio signal. These changes do not
repair distortion already present in the microphone's USB conversion or Windows
input processing, and do not guarantee recovery after true hardware dropouts.
