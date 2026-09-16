# LVGL dependency

`lvgl/` contains the LVGL source snapshot originally bundled in lvgl-com commit
`fd5cb184d8a481668fc85f14d8da7cf9acdfc96f` at
https://codeberg.org/resiliencetheatre/lvgl-com.git, subdirectory `lvgl/`.
Its version header reports **9.5.0**. The original import did not record an
upstream LVGL commit; do not assume this is identical to an upstream release tag.

Only these upstream paths are retained:

- `src/` (including dependency headers and embedded license notices)
- `CMakeLists.txt`, `env_support/cmake/os_desktop.cmake`,
  `env_support/cmake/version.cmake`
- `lvgl.h`, `lvgl_private.h`, `lv_version.h`, `lv_version.h.in`, `lvgl.pc.in`
- `lv_conf_template.h`, `LICENCE.txt`

Tests, demos, examples, documentation, CI, scripts and unrelated platform
integration bundles are excluded. Optional library features are disabled by the
application configuration; the source/header tree is kept intact to avoid
maintaining a private fork of its include graph.

Application code and configuration must stay outside this directory.
`SHA256SUMS` records the installed vendor tree, including the patches below.
Verify it from this directory with `sha256sum -c SHA256SUMS`.

## Local patches

`patches/0001-wayland-initialization.patch` is already applied:

1. Return NULL when the initial Wayland connection fails. Previously window
   creation continued with an uninitialized list and corrupted the heap.
2. Accept an acknowledged XDG configure event when the compositor chooses the
   window's existing dimensions. Previously this triggered an assertion because
   no resize was pending.

Both were reproduced with headless Weston during this port. Review and drop each
fix once a replacement LVGL version includes it. No UI-specific logic belongs in
these patches.

## Upgrade procedure

1. Obtain an explicit upstream LVGL release/commit and record its URL, revision
   and archive checksum here. Keep a clean copy of the upstream source.
2. Test it before replacing the vendor tree by configuring a new build directory
   with `-DLVGL_SOURCE_DIR=/absolute/path/to/lvgl`. Apply any still-needed patches
   to that test copy with `patch -p1 < /path/to/patch` from its root.
3. Compare its configuration template and Wayland APIs with `config/lv_conf.h`,
   `cmake/Wayland.cmake` and `src/display.c`. In particular, the current snapshot
   dispatches Wayland through an LVGL timer despite its stale header declaration
   of `lv_wayland_timer_handler`; the application uses `lv_timer_handler`.
4. Build both backends, cross-build for Pi, test missing-compositor failure and
   fullscreen rendering/resizing under a kiosk compositor, then test Pi touch.
5. Replace `lvgl/` with the listed source/build/license paths from the validated
   version, update the local patches and this provenance, and regenerate the
   manifest. Retain additional files if the new upstream build requires them.

There are no downloads during application configuration or compilation.
