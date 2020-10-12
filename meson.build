project('rwte', ['c', 'cpp'],
    version: '1.0.0',
    default_options: [
        'cpp_std=c++17',
        'b_lto=true',
        'b_ndebug=if-release'
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

librw_proj = subproject('librw')
librw_dep = librw_proj.get_variable('librw_dep')

rwte_common_deps = [
    xdg_basedir,
    cairo,
    pangocairo,
    xkbc,
    lua,
    ev,
    util,
    rt
]

inc = include_directories('include')

common_sources = files(
    'src/lua/config.cpp',
    'src/lua/logging.cpp',
    'src/lua/state.cpp',
    'src/lua/term.cpp',
    'src/lua/window.cpp',

    'src/profiling.cpp',
    'src/reactor.cpp',
    'src/renderer.cpp',
    'src/rwte.cpp',
    'src/screen.cpp',
    'src/selection.cpp',
    'src/sigevent.cpp',
    'src/term.cpp',
    'src/tty.cpp',
    'src/utf8.cpp',
    'src/window.cpp'
)

subdir('src')
subdir('docs')

executable(
    'rwte-bench', [
        'bench/main.cpp',
        'bench/misc.cpp',
        'bench/utf8.cpp',
        'test/utf8-data.cpp',
        common_sources
    ],
    dependencies: [
        rwte_common_deps,
        librw_dep
    ],
    include_directories : inc
)

testexe = executable(
    'rwte-test', [
        'test/main.cpp',
        'test/screen.cpp',
        'test/utf8.cpp',
        'test/utf8-data.cpp',
        common_sources
    ],
    dependencies: [
        rwte_common_deps,
        librw_dep
    ],
    include_directories: inc
)

test('rwte test', testexe)
