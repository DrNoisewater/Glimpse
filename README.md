# Glimpse 0.2.0

A small, local Windows region recorder for Noisewater Labs. Native C++20 / Win32,
Windows.Graphics.Capture, Direct3D 11 and Media Foundation. No browser runtime,
FFmpeg download, account, server, telemetry or upload integration.

**Delivery status: source implementation, not a validated Windows release.**
The authoring environment was Linux without a Windows SDK or Windows runtime.
There is **no compiled EXE in this archive**. The Windows source has not been
compiled or run here. Portable policy tests passed; that does not validate the
recorder. Build and run the Windows acceptance checklist before relying on it.

## Build a portable executable

Requirements: Windows, Visual Studio 2022 or newer with **Desktop development
with C++**, CMake tools, and Windows 10/11 SDK **10.0.19041 or newer**. SDK
C++/WinRT headers are used directly; no NuGet dependencies.

1. Extract this folder to a local directory.
2. Run `build-windows.cmd` from a Developer Command Prompt, or double-click it
   if CMake is available on PATH.
3. On a successful build, run `portable\Glimpse.exe`.

The script compiles Release x64, runs policy tests, and creates
`Glimpse-Windows-x64.zip`. The executable statically links the MSVC runtime;
Windows supplies the capture, graphics and codec APIs. No installer is needed.
Build outputs are not included until an actual Windows build succeeds.

Equivalent commands:

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix portable
```

The included GitHub Actions workflow builds the same target and uploads a
portable artifact when this directory is placed at a repository root. It has
not been submitted or executed as part of this delivery.

## Use

1. Launch Glimpse. It appears in the system tray, possibly inside the hidden
   icons menu. There is no persistent main window.
2. Choose **Audio for next recording** in the tray menu if you want narration or system sound.
3. Press **Ctrl+Shift+R**, drag a rectangle, and release: recording starts automatically.
4. Press the shortcut again or click **Stop** to finalize the MP4.
5. Paste into a destination that accepts file clipboard uploads, or hold and
   drag **Drag to share** from the saved-capture panel. **Open folder** selects
   the file in Explorer.

A blinking red dot appears at the selected area’s top-right corner. A thin red
border identifies the recording region. The controls attach near that corner and
can be moved. Paused is amber; starting/saving is violet. These overlays are
excluded from capture.

**Pause / Resume** excludes paused time. **Discard** discards the current
recording; **Escape** cancels selection. Closing the recording toolbar saves
an active recording. Exiting from the tray also saves first. A display-layout
change, device error, or session lock stops capture and attempts to save what
has already been encoded.

Right-click the tray icon for New Recording, Recent Captures (last ten), Open
Captures Folder, Settings, or Exit. Clicking a recent capture opens its share
panel. Settings offers the shortcut, folder, quality, frame-rate ceiling,
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
- Saturation prevents numeric overflow, but two loud sources can still distort;
  reduce levels if they reach the orange meter range. There is no mastering limiter.
- Muting releases that endpoint. Pause discards audio rather than saving it;
  an enabled microphone may still show Windows’ microphone-use indicator.
- If a selected device cannot open or is disconnected, the app reports the
  problem and attempts to save the usable recording. Device hot-swapping and
  a dedicated device picker are not implemented; choose defaults in Windows.
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
  No administrator privileges are requested. Disable this setting before
  deleting or moving the portable EXE.
- Each recording has a `.mp4.metrics.txt` sidecar with local measurements.
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
- Each submitted frame owns its own NV12 texture, avoiding premature reuse by
  an asynchronous encoder. A reusable surface pool is a future optimization.
- Theme is read at launch. Rounded controls, custom-colored native sliders,
  Segoe UI Variable Text on Windows 11 (Segoe UI on Windows 10), and a native
  save toast share one visual system. Combo boxes and hotkey controls retain
  Windows styling. Visual layout/accessibility still require Windows verification.

See `docs/TESTING.md`, `docs/PERFORMANCE.md` and `docs/ARCHITECTURE.md`.

### Microphone and system audio crackle

The audio capture path now uses **two dedicated WASAPI worker threads**, one for the
Windows default microphone and one for default playback loopback. This prevents
blocking H.264 encoding, GPU conversion, or MP4 I/O on the screen-recording
thread from starving both audio capture buffers. The audio samples still mix
on a synchronized timestamped 48-kHz stereo ring, with a final 300-ms output
staging margin. See `AUDIO-THREAD-FIX-NOTES.txt` for details and limitations.

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

After each recording, open `Capture_...mp4.metrics.txt` beside the video:

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
