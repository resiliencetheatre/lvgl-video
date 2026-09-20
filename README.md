# lvgl-video

A single-page LVGL local camera preview for Raspberry Pi 4 and its OV5647
Camera Module v1.3. GStreamer captures 640x480 NV12 at 10 fps through
`libcamerasrc`, converts to RGB565, and delivers frames through `appsink`.
LVGL fits the image into the video area while preserving its aspect ratio,
with Mic, Camera and End call placeholder buttons below. There is no audio,
network transport or button action yet.

LVGL alone writes the framebuffer. Capture runs in GStreamer's threads; the
UI polls a one-frame appsink queue without blocking. Frames are copied into
application-owned memory on the LVGL thread before rendering. Old queued
frames are dropped rather than accumulating delay. Camera errors appear in
the application log and leave an error message over the video area.

## Source layout

- `src/main.c`: initialization, event loop and signal handling.
- `src/ui.c`: camera image and placeholder controls.
- `src/camera.c`: fixed camera pipeline, frame handoff and cleanup.
- `src/display.c`: framebuffer (default) or Wayland display and touch input.
- `config/lv_conf.h`: application-owned LVGL configuration.
- `third_party/`: vendored LVGL and documented patches/provenance.

The adapter uses an LVGL image with GStreamer appsink directly so camera caps
can be specified explicitly. It does not enable LVGL's bundled generic
GStreamer player. The pipeline follows the working capture setup documented
in `rpi-extree/board/raspberrypi/README-videoterm-rpi4-camera.md`, at 10 fps.

## Buildroot and target test

`rpi-extree/package/lvgl-video` builds the sibling local `lvgl-video` directory.
Keep libcamera and its `rpi/vc4` pipeline enabled in the existing Pi 4 camera
configuration, and select `BR2_PACKAGE_LVGL_VIDEO=y` in menuconfig. Leave
`BR2_PACKAGE_LVGL_VIDEO_WAYLAND` disabled for framebuffer use. The package
selects the GStreamer parser, app and video conversion plugins; Buildroot
also enables libcamera's GStreamer source when these packages are selected.

From Buildroot, using your actual output directory:

```sh
make O=output menuconfig
make O=output lvgl-video
make O=output
```

For later application edits, use `make O=output lvgl-video-rebuild all`.
Install/boot the resulting image before testing. Existing defconfigs and
startup services still select and launch `lvgl-com`; this test does not replace
them automatically. Over SSH or serial on the target:

```sh
systemctl stop lvgl-com.service getty@tty1.service
# If Motion is running, stop it so the camera is free:
systemctl stop motion
/usr/bin/lvgl-video
```

The preview starts automatically. Press Ctrl+C to stop and release the camera.
Restore the old UI/console with:

```sh
systemctl start getty@tty1.service lvgl-com.service
```

Restart Motion too if it was previously running. Do not run a separate camera
capture pipeline or `fbdevsink` alongside this application. For diagnostics:

```sh
gst-inspect-1.0 libcamerasrc
gst-inspect-1.0 appsink
gst-inspect-1.0 videoconvert
GST_DEBUG=2 /usr/bin/lvgl-video
```

The framebuffer backend uses `/dev/fb0` (override with `LV_LINUX_FBDEV_DEVICE`)
and `/dev/input/touchscreen`. Run with access to these devices. SIGINT/SIGTERM
stop the application. No configuration file or image assets are required.

## Native build and test pattern

Requires C/C++ compilers, CMake, pkg-config, pthreads, and GStreamer app/video
development libraries. Runtime capture also needs libcamera's GStreamer
plugin and the app/video conversion plugins.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j
./build/lvgl-video
```

For a camera-independent display test, install/enable `videotestsrc` (Buildroot
option `BR2_PACKAGE_GST1_PLUGINS_BASE_PLUGIN_VIDEOTESTSRC`) and run:

```sh
LVGL_VIDEO_TEST_PATTERN=1 ./build/lvgl-video
```

Optional Wayland builds use `-DLVGL_VIDEO_BACKEND=wayland` and additionally
require Wayland client/cursor libraries, libxkbcommon, wayland-protocols and
wayland-scanner. Run inside an existing compositor session.

The application and external tree are separate Git repositories.
