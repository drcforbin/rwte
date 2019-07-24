project('rwte', ['c', 'cpp'],
    version: '1.0.0',
    default_options: [
        'cpp_std=c++17',
        'b_lto=true'
    ])

cc = meson.get_compiler('c')

xdg_basedir = dependency('libxdg-basedir')
xcb = dependency('xcb')
xcb_util = dependency('xcb-util')
cairo = dependency('cairo')
pangocairo = dependency('pangocairo')
xkbc = dependency('xkbcommon')
xkbc_x11 = dependency('xkbcommon-x11')
xcd_xkb = dependency('xcb-xkb')
wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
lua = dependency('lua')
ev = cc.find_library('ev')
util = cc.find_library('util')
rt = cc.find_library('rt')


rwte_deps = [
    xdg_basedir,
    xcb,
    xcb_util,
    cairo,
    pangocairo,
    xkbc,
    xkbc_x11,
    xcd_xkb,
    wayland_client,
    wayland_cursor,
    lua,
    ev,
    util,
    rt,
]

inc = include_directories('include')

subdir('src')
subdir('docs')

