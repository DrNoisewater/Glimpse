# Implementation notes

`src/app.cpp` owns the Win32 message loop, global hotkey, tray icon, region
overlay, toolbar, settings and share panel. It initializes OLE on the UI thread
for Explorer file drag/drop. A single-instance mutex prevents hotkey competition
between copies. Per-monitor-v2 awareness keeps selection coordinates physical.

`src/capture.cpp` owns an MTA recording thread and initializes Media Foundation
only for active recordings. It creates a WGC item/session/free-threaded pool for
each monitor intersecting the requested desktop rectangle. Bounded two-frame
pools are polled, and new monitor pixels are cropped into a persistent BGRA
canvas using D3D11 CopySubresourceRegion. Gaps remain black. The cursor is included
by WGC's default capture behavior. Windows capture indicators are not disabled.

The D3D11 video processor scales the canvas and converts to limited-range
BT.709 NV12. Surfaces come from a pool and return to it only when the encoder releases
their tracked sample, so encoder retention cannot cause a
later frame to overwrite an earlier one. GPU-input encoding uses a DXGI device
manager. Initialization failures retry a software-encoder path with NV12 staging
readback; the common path avoids CPU copies of pixel data.

Media Foundation's synchronous sink writer encodes H.264 into MPEG-4. Its normal
backpressure is retained; there is no unbounded application frame queue. A
monotonic timeline and one pending sample let each sample's duration extend to
the actual next sample timestamp. Late frames therefore reduce smoothness
instead of making the clip play too fast. Paused time is removed. Static scenes
reuse the latest canvas and are still submitted at the selected cadence; H.264
inter-frame coding and requested quality VBR handle compression.

The worker finalizes the sink before publishing the result. A fail-if-exists
byte stream reserves the temporary filename. The final rename does not allow
replacement. Capture failures attempt to flush the pending sample and finalize
the usable prefix; encoder/disk failures may leave an unrecoverable partial.

The UI receives WM_CAPTURE_STARTED and WM_CAPTURE_FINISHED messages. Stop only
sets a worker flag; finalization never blocks the interactive message loop.
Joining at completion is safe because the result has already been posted.
Windows end-session handling may wait for the worker; forced termination still
cannot guarantee a finalized MP4.

The completed file is put on the clipboard as CF_HDROP, with bounded timer-based
retries. Explorer's shell IDataObject supports native file dragging. No browser
or destination-specific assumptions are built into this path.

## Reference documentation consulted

- [WGC monitor interop](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createformonitor)
- [Free-threaded frame pool](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframepool.createfreethreaded)
- [D3D surface interop](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.directx.direct3d11.interop/ns-windows-graphics-directx-direct3d11-interop-idirect3ddxgiinterfaceaccess)
- [Sink-writer attributes](https://learn.microsoft.com/en-us/windows/win32/medfound/sink-writer-attributes)
- [Sink writer and byte streams](https://learn.microsoft.com/en-us/windows/win32/api/mfreadwrite/nf-mfreadwrite-mfcreatesinkwriterfromurl)
- [DXGI surface buffers](https://learn.microsoft.com/en-us/windows/win32/api/mfapi/nf-mfapi-mfcreatedxgisurfacebuffer)
- [DXGI device manager and multithread protection](https://learn.microsoft.com/en-us/windows/win32/api/mfobjects/nf-mfobjects-imfdxgidevicemanager-resetdevice)

These references support API choices. They do not substitute for compiling or
running this implementation on Windows.

## Version 0.2 audio and UI

`audio.cpp` opens WASAPI shared-mode PCM capture for the default microphone and
loopback capture for the default playback endpoint. Windows audio-engine
conversion requests stereo 48 kHz PCM16. Timestamped packets feed a bounded,
two-second ring shared by both inputs; each has independent gain. The AAC track
is registered before sink-writer BeginWriting. Silent mode has no audio stream.

QPC packet timestamps and video timestamps share 100 ns units. Audio uses a
100 ms interleave cushion. Source data before the current active interval is
trimmed, which prevents pre-start or paused audio from leaking into the result.
The output timeline fills packet gaps with silence; late samples are discarded
and counted. Finalization drains audio to the video end time. Capture uses the
recording thread; prolonged encoder blocking can overrun the 200 ms WASAPI
buffer, so real hardware stress testing is still necessary.

Sources are opened/closed on the MTA thread when toggled. Gain and peak-meter
values cross the UI boundary through atomics. WASAPI failures are not silently
converted to a successful silent recording; they stop the run with an error.

The region's click-through frame and blinking dot are separate capture-excluded
windows. Drag-release starts immediately; the toolbar is top-right anchored,
then draggable. Its sliders keep native keyboard behavior while custom draw
provides consistent colors. A non-activating native toast handles saved-file
feedback with interaction-aware timeout. No web UI runtime was introduced.

Additional references:
- https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer
- https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording
- https://learn.microsoft.com/en-us/windows/win32/medfound/aac-encoder
