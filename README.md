# lvgl-com

LVGL communications terminal application for Linux. The default backend is a
Wayland client for Weston kiosk; a framebuffer backend remains available for
the original appliance target. The Wayland UI uses software-rendered shared
memory buffers. Weston performs GPU composition. No GTK, LVGL demo application,
GStreamer or application-side EGL integration is required for this stage.

## Source layout

```text
src/                 application UI, display adapter, INI and logging helpers
config/lv_conf.h     application-owned LVGL configuration
assets/              installed UI images
cmake/Wayland.cmake   host scanner and XDG protocol generation
third_party/lvgl/    isolated LVGL source dependency
third_party/patches/ documented fixes to the imported snapshot
```

See `third_party/README.md` for provenance and the upgrade procedure. Vendor
examples, demos, tests and unrelated integration files have been removed. LVGL
is linked privately into the application; installation does not export its
headers or libraries. Generated protocol files and binaries stay in the build
directory.

## Native build

Requires a C/C++ compiler, CMake, pkg-config, pthreads, libpng/zlib, Wayland client
and cursor libraries, libxkbcommon, wayland-protocols and a native wayland-scanner.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j
```

For execution without installation, additionally configure
`-DLVGL_COM_ASSET_DIR=/absolute/path/to/lvgl-com/assets`. Start the binary from a
directory containing `lvgl.ini`; it reads and updates that file relative to its
working directory. Optional communications/status services are separate from
display initialization.

Under an existing Wayland session, run `./build/lvgl-com`. For a framebuffer
build use a separate directory and `-DLVGL_COM_BACKEND=fbdev`.

The Wayland path uses the compositor's touch input and configured window size.
Framebuffer console unbinding and framebuffer power controls are not used, and
their settings are hidden. Physical brightness and idle policy remain outside
this initial Wayland UI port.

## Buildroot

The external tree's `package/lvgl-com` builds this **local checkout**, not the old
remote package revision. Select `BR2_PACKAGE_LVGL_COM=y` and
`BR2_PACKAGE_LVGL_COM_WAYLAND=y` for Weston. The recipe supplies the host scanner
and target protocol directory explicitly, and installs `/usr/bin/lvgl-com` and
`/usr/share/lvgl-com/link.png`.

After changing from the previous package layout, run from Buildroot:

```sh
make O=output-videoterm-weston BR2_EXTERNAL=../rpi-extree raspberrypi5_videoterm_weston_defconfig
make O=output-videoterm-weston lvgl-com-dirclean
make O=output-videoterm-weston
```

Use your actual output directory. A clean package build avoids old demo objects
or cached paths surviving the source reorganization. For later local edits,
`make O=output-videoterm-weston lvgl-com-rebuild all` resynchronizes the local
source and regenerates the image.

The target's `lvgl-com-wayland.service` starts after Weston signals readiness,
uses `/opt/lvgl-com` as its working directory and connects to
`/run/weston-kiosk/wayland-0`. The old framebuffer service stays masked. Restart
the UI with `systemctl restart lvgl-com-wayland`; inspect its log with
`journalctl -b -u lvgl-com-wayland`.

This directory is a separate Git repository from the external Buildroot tree.
Commit the application changes here and package/overlay changes in the parent
repository together when publishing this port.
