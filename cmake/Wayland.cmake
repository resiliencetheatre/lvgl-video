find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client wayland-cursor xkbcommon)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols>=1.25)
# Buildroot supplies an explicit sysroot path and a host-native scanner.
set(WAYLAND_PROTOCOLS_DIR "" CACHE PATH "Target wayland-protocols data directory")
if(NOT WAYLAND_PROTOCOLS_DIR)
    pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
endif()
find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner REQUIRED)
set(protocol_xml "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
if(NOT EXISTS "${protocol_xml}")
    message(FATAL_ERROR "Missing XDG shell protocol: ${protocol_xml}")
endif()
set(protocol_dir "${CMAKE_CURRENT_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${protocol_dir}")
add_custom_command(
    OUTPUT "${protocol_dir}/wayland_xdg_shell.h" "${protocol_dir}/wayland_xdg_shell.c"
    COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${protocol_xml}" "${protocol_dir}/wayland_xdg_shell.h"
    COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${protocol_xml}" "${protocol_dir}/wayland_xdg_shell.c"
    DEPENDS "${protocol_xml}"
    VERBATIM)
add_custom_target(lvgl-video-protocols DEPENDS
    "${protocol_dir}/wayland_xdg_shell.h" "${protocol_dir}/wayland_xdg_shell.c")
add_dependencies(lvgl lvgl-video-protocols)
add_library(lvgl-video-wayland-protocol STATIC "${protocol_dir}/wayland_xdg_shell.c")
add_dependencies(lvgl-video-wayland-protocol lvgl-video-protocols)
target_link_libraries(lvgl-video-wayland-protocol PRIVATE PkgConfig::WAYLAND)
target_include_directories(lvgl PRIVATE "${protocol_dir}")
target_link_libraries(lvgl PUBLIC PkgConfig::WAYLAND lvgl-video-wayland-protocol)
