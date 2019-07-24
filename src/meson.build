version_file = configure_file(
    input: 'version.cpp.in',
    output: 'version.cpp',
    configuration: {
      'version': meson.project_version()
    }
)

common_sources = files(
    'fmt/format.cc',
    'fmt/posix.cc',

    'lua/config.cpp',
    'lua/logging.cpp',
    'lua/state.cpp',
    'lua/term.cpp',
    'lua/window.cpp',

    'bufferpool.cpp',
    'logging.cpp',
    'profiling.cpp',
    'renderer.cpp',
    'rwte.cpp',
    'screen.cpp',
    'selection.cpp',
    'sigwatcher.cpp',
    'term.cpp',
    'tty.cpp',
    'utf8.cpp',
    'window.cpp',
    'wlwindow.cpp',
    'xcbwindow.cpp',

    'xdg-shell/xdg-shell-client-protocol.c'
)

executable(
    'rwte', [
        'rwte-main.cpp',
        common_sources,
        version_file
    ],
    dependencies: rwte_deps,
    include_directories: inc
)

testexe = executable(
    'rwte-test', [
        'rwte-test.cpp',
        'screen-test.cpp',
        'utf8-test.cpp',
        common_sources
    ],
    dependencies: rwte_deps,
    include_directories: inc
)

test('rwte test', testexe)
