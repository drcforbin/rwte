version_file = configure_file(
    input: 'version.cpp.in',
    output: 'version.cpp',
    configuration: {
      'version': meson.project_version()
    }
)

xcb_deps = [
    xcb,
    xcb_util,
    xcd_xkb,
    xkbc_x11
]

xcb_sources = [
    'xcbwindow.cpp'
]

wayland_deps = [
    wayland_client,
    wayland_cursor
]

wayland_sources = [
    'bufferpool.cpp',
    'wlwindow.cpp',
    'xdg-shell/xdg-shell-client-protocol.c',
]

executable(
    'rwte', [
        'rwte-main.cpp',
        common_sources,
        xcb_sources,
        wayland_sources,
        version_file
    ],
    dependencies: [
        rwte_common_deps,
        wayland_deps,
        xcb_deps,
        librw_dep
    ],
    include_directories: inc
)

executable(
    'rwte-xcb', [
        'rwte-main.cpp',
        common_sources,
        xcb_sources,
        version_file
    ],
    dependencies: [
        rwte_common_deps,
        xcb_deps,
        librw_dep
    ],
    include_directories: inc,
    cpp_args : '-DRWTE_NO_WAYLAND'
)

executable(
    'rwte-wayland', [
        'rwte-main.cpp',
        common_sources,
        wayland_sources,
        version_file
    ],
    dependencies: [
        rwte_common_deps,
        wayland_deps,
        librw_dep
    ],
    include_directories: inc,
    cpp_args : '-DRWTE_NO_XCB'
)
