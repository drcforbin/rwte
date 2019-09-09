doc_targets = []

# todo: use get_option to decide whether to enable docs?
doxygen = find_program('doxygen', required: false)
if doxygen.found()
    conf_data = configuration_data()
    conf_data.set('DOXYGEN_INPUT', meson.source_root())
    conf_data.set('DOXYGEN_OUTPUT', meson.current_build_dir())

    doxyfile = configure_file(
        input: files('Doxyfile.in'),
        output: 'Doxyfile',
        configuration: conf_data
    )

    doc_targets += custom_target('cdocs',
        build_by_default: true,
        command: [doxygen, doxyfile],
        output: 'html'
    )
endif

ldoc = find_program('ldoc', required: false)
if ldoc.found()
    ldoc_config = files('config.ld')
    mod_logging = files('../src/lua/logging.cpp')
    mod_term = files('../src/lua/term.cpp')
    mod_window = files('../src/lua/window.cpp')
    out_path = meson.current_build_dir() #rjoin_paths(meson.current_build_dir(), 'ldocs')

    # would love to use a generator to save repition, but
    # haven't yet worked out how
    #g = generator(ldoc,
    #    arguments: ['--quiet',
    #        '--config', 'config.ld',
    #        '--dir', '@BUILD_DIR@',
    #        '--output', '@BASENAME@',
    #        '@INPUT@'],
    #    output: '@BASENAME@.html')
    #
    #g.process(mod_logging, mod_term, mod_window)

    doc_targets += custom_target('ldocs_logging',
        build_by_default: true,
        command: [ldoc, '--quiet',
            '--config', ldoc_config,
            '--dir', out_path,
            '--output', 'logging',
            mod_logging],
        input: [ldoc_config, mod_logging],
        output: 'logging.html'
    )

    doc_targets += custom_target('ldocs_term',
        build_by_default: true,
        command: [ldoc, '--quiet',
            '--config', ldoc_config,
            '--dir', out_path,
            '--output', 'term',
            mod_term],
        input: [ldoc_config, mod_term],
        output: 'term.html'
    )

    doc_targets += custom_target('ldocs_window',
        build_by_default: true,
        command: [ldoc, '--quiet',
            '--config', ldoc_config,
            '--dir', out_path,
            '--output', 'window',
            mod_window],
        input: [ldoc_config, mod_window],
        output: 'window.html'
    )
endif

if doc_targets.length() > 0
  run_target('docs',
      depends: doc_targets,
      command: ['echo', 'done'])
endif

