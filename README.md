# lvgl-video

Framebuffer LVGL camera preview and two-way GTK Pipe-compatible RTP/UDP calls.
The Pi's OV5647 is captured through `libcamerasrc` at fixed **640x480, 10 fps**.
The main picture shows received video, with a 160x120 local preview in its
upper-right corner. LVGL alone writes the framebuffer. Microphone and speaker
use ALSA, with echo cancellation enabled by default for speaker use.

The three buttons mute/unmute the outgoing microphone, start media, and end the
call. Ending a call releases camera, audio devices and media sockets. Heartbeat
and control reception stay active so the call can be restarted. Without
`--peer`, the application retains its original local-only camera preview and
starts automatically without opening audio or network sockets.

There is no smartcard integration, secure mode, quality slider or adaptive quality. All traffic is plaintext (unencrypted).

## Test against GTK Pipe

For example, Pi = `192.168.1.10`, desktop = `192.168.1.20`.
On the Pi, stop the old framebuffer UI/getty and any process using the camera
(such as Motion), then run over SSH or serial:

```sh
systemctl stop lvgl-com.service getty@tty1.service
lvgl-video --peer 192.168.1.20 --start
```

On the desktop:

```sh
gtk-pipe --peer 192.168.1.10
```

Press **Start stream** in GTK Pipe. Omit `--start` on the Pi to start with its
Start button instead. Both ends must start media; an incoming start message
only displays a notice, it does not activate camera/microphone automatically.
GTK Pipe's quality controls can be left at a modest setting; lvgl-video always
sends 640x480 at 10 fps regardless of the remote setting. Incoming video is
scaled/letterboxed to 640x480, then fitted into the available LVGL video area.

Use the other Pi's address in `--peer` for Pi-to-Pi calls. Ports must match on
both ends, and the route/firewalls must permit the three UDP ports below.
There is no signalling server or NAT traversal.

### Echo cancellation

Echo cancellation is enabled by default, matching GTK Pipe: microphone capture
passes through `webrtcdsp`, using decoded playback from `webrtcechoprobe` as its
reference. Noise suppression and a high-pass filter are enabled; automatic
gain control is disabled. The microphone mute is applied after capture DSP.

For headphones, or to compare the unprocessed audio path, disable it with:

```sh
lvgl-video --peer 192.168.1.20 --start --disable-echo-cancellation
```

GTK Pipe accepts the same `--disable-echo-cancellation` switch. Each side chooses
its own processing independently; codecs, ports and RTP framing are unchanged.
Local camera-only mode does not create audio or echo cancellation elements.

Verify the new plugin on the Pi after installing the rebuilt image:

```sh
gst-inspect-1.0 webrtcdsp
gst-inspect-1.0 webrtcechoprobe
```

Both elements are required in the default mode. If they are missing, the media
pipeline reports an error; the disable flag explicitly uses the previous audio
path. Assess echo reduction and CPU load on the actual speaker/microphone setup;
synthetic transport tests cannot measure acoustic cancellation quality.

Choose a USB headset explicitly if ALSA `default` is not the desired device:

```sh
arecord -l
aplay -l
lvgl-video --peer 192.168.1.20 --start \
  --audio-input plughw:1,0 --audio-output plughw:1,0
```

`plughw:1,0` is an example; use the actual capture/playback device identifiers.
The application does not alter mixer levels or routing. A missing camera or
failed audio device stops the media pipeline and reports an error; correct
the device setting and restart the application, or press Start to retry a
transient failure.

Ctrl+C stops media, notifies the peer and exits. End call sends the same stop
notice while leaving the application open. Receiving GTK Pipe's stop notice
also stops local media. Restore the old UI/console after exiting if needed:

```sh
systemctl start getty@tty1.service lvgl-com.service
```

## Wire compatibility

| Channel | Default UDP port | Format |
| --- | --- | --- |
| Video | 5000 | VP8 RTP, payload 96, clock rate 90000 |
| Audio | 5002 | Opus RTP, payload 97, clock rate 48000 |
| Control/text | 5004 | GTKPIPE/1 PING, PONG, STREAM_STARTED, STREAM_END, TEXT |

VP8 uses a fixed 600 kbit/s target, real-time encoding and a maximum keyframe
spacing of 30 frames. Opus uses 32 kbit/s, mono 48 kHz capture and in-band FEC;
receive enables packet loss concealment. Both receivers use GTK Pipe's 120 ms
RTP jitter buffer. No RTCP/session negotiation is required by this protocol.
The remote can use a different resolution, frame rate or bitrate.

Heartbeats run every two seconds, with a six-second peer-reachability timeout.
The indicator reports control-channel reachability, not proof of media delivery.
The last remote image is hidden after three seconds without a new frame.
Incoming GTK Pipe text messages are printed to stderr; there is no chat editor
in this video UI. Outgoing control messages are fully compatible with GTK Pipe.
Video, audio and control are all unencrypted and unauthenticated.

Additional options:

```sh
lvgl-video --peer 10.10.0.2 --bind 10.10.0.1 \
  --video-port 5000 --audio-port 5002 --text-port 5004 --rtp-mtu 1100
lvgl-video --help
```

Peer and bind addresses must be numeric IPv4 or IPv6 and use the same family.
Without `--bind`, receivers listen on all addresses of the peer's family.
`--rtp-mtu` limits the complete outgoing video RTP UDP payload (default 1400),
matching GTK Pipe's option; audio packetization is unchanged.

## Buildroot

The package requires libcamera with its `rpi/vc4` pipeline and selects GStreamer
app, video conversion/scaling, ALSA, audio conversion/resampling, Opus, VP8,
RTP, RTP jitter buffering, UDP and WebRTC DSP plugins. There are no GTK dependencies in the
application. Keep `BR2_PACKAGE_LVGL_VIDEO_WAYLAND` disabled for framebuffer use.

The package fetches a pinned Git revision. These working-tree changes are not
part of that remote revision until published and the pin is updated. To test
this local checkout now, add this line to your Buildroot output directory's
`local.mk` (use the absolute path to your checkout):

```make
LVGL_VIDEO_OVERRIDE_SRCDIR = /home/tech/laboratory/rpi4/lvgl-video
```

Then run from Buildroot, substituting the actual output directory:

```sh
make O=output olddefconfig
make O=output libvpx opus webrtc-audio-processing
make O=output gst1-plugins-base-reconfigure
make O=output gst1-plugins-good-reconfigure
make O=output gst1-plugins-bad-reconfigure
make O=output lvgl-video-rebuild all
```

Existing GStreamer packages must be reconfigured after enabling new plugin
options; Buildroot does not automatically rebuild them for configuration
changes. The image rebuild is needed to include the newly selected codecs/plugins.
Rebuild libcamera too if it was originally built without GStreamer support.
Install/boot the updated image before testing. Existing startup services are
not changed by this application update.

For diagnostics on the target:

```sh
gst-inspect-1.0 libcamerasrc
gst-inspect-1.0 vp8enc
gst-inspect-1.0 opusenc
gst-inspect-1.0 alsasrc
gst-inspect-1.0 alsasink
GST_DEBUG=2 lvgl-video --peer 192.168.1.20 --start
```

## Native build and automated tests

Requires C/C++ compilers, CMake, pkg-config, pthreads, GLib/GIO and GStreamer
app/video development libraries. Runtime media needs the plugins listed above.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j
./build/lvgl-video
```

Framebuffer defaults to `/dev/fb0` (override with `LV_LINUX_FBDEV_DEVICE`),
with touch at `/dev/input/touchscreen`. Optional Wayland builds use
`-DLVGL_VIDEO_BACKEND=wayland` and require the Wayland client/cursor libraries,
libxkbcommon, wayland-protocols and wayland-scanner.

`--test-media` substitutes live test video/audio and discards received audio;
it never opens the camera, microphone or speaker. It requires `videotestsrc`
and `audiotestsrc`, which the production Buildroot package does not select.
`LVGL_VIDEO_TEST_PATTERN=1` remains supported for video-only substitution.

The native interoperability test uses the sibling `gtk-pipe` checkout's actual
pipeline builder, substituting only hardware/display endpoints. GTK3 development
libraries and GStreamer's clockoverlay are needed for this test, not the app:

```sh
cmake -S . -B build-test -DLVGL_VIDEO_TESTS=ON
cmake --build build-test -j
ctest --test-dir build-test --output-on-failure
```

Override `GTK_PIPE_SOURCE_DIR` if that reference checkout is elsewhere. The test
exchanges VP8 and Opus in both directions over loopback, renders received frames
through an offscreen LVGL display, checks local preview, heartbeat/start/stop,
mute and restart with echo cancellation both enabled and disabled. Argument
validation is tested separately. These tests do not
replace a real two-device camera/headset call.

## Source layout

- `src/options.c`: command-line parsing and address/port validation.
- `src/session.c`: media pipelines, control socket, heartbeat and lifecycle.
- `src/video.c`: nonblocking appsink-to-LVGL frame handoff, respecting stride.
- `src/ui.c`: remote video, local preview, status and controls.
- `src/main.c`, `src/display.c`: event loop and display backend.
- `config/lv_conf.h`, `third_party/`: LVGL configuration and vendored source.

Capture is shared between transmission and local preview; the camera is opened
only once. Frame queues are bounded and discard old frames. LVGL receives
stable application-owned RGB565 buffers, updated only on its main thread.
