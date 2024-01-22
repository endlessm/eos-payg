subdir('plugins')

deps = [
  glib_dep,
  gobject_dep,
  gio_dep,
  libgsystemservice_dep,
  libeos_payg_dep,
]

envs = test_env + [
  'G_TEST_SRCDIR=' + meson.current_source_dir(),
  'G_TEST_BUILDDIR=' + meson.current_build_dir(),
]

test_programs = {
  'manager' : {},
  'multi-task' : {},
  'service' : {},
  'provider-loader' : {},
  'boottime-source' : {},
  'clock-jump-source' : {'suites': ['unsafe']},
}

installed_tests_metadir = join_paths(datadir, 'installed-tests',
                                     'libeos-payg-' + eos_payg_api_version)
installed_tests_execdir = join_paths(libexecdir, 'installed-tests',
                                     'libeos-payg-' + eos_payg_api_version)

foreach program_name, extra_args : test_programs
  suites = extra_args.get('suites', [])
  if suites.contains('unsafe') and want_tests != 'unsafe'
    message('@0@ is an unsafe test; use -Dtests=unsafe to enable'.format(program_name))
  else
    test_conf = configuration_data()
    test_conf.set('installed_tests_dir', installed_tests_execdir)
    test_conf.set('program', program_name)

    configure_file(
      input: test_template,
      output: program_name + '.test',
      install: enable_installed_tests,
      install_dir: installed_tests_metadir,
      configuration: test_conf,
    )

    exe = executable(
      program_name,
      [program_name + '.c'],
      dependencies: deps,
      include_directories: root_inc,
      install: enable_installed_tests,
      install_dir: installed_tests_execdir,
      install_rpath: pkglibdir,
    )

    test(
      program_name,
      exe,
      env: envs,
      suite: ['eos-payg'] + suites,
    )
  endif
endforeach
