python_mod = import('python')
py3 = python_mod.find_installation('python3')

envs = test_env + [
  'G_TEST_SRCDIR=' + meson.current_source_dir(),
  'G_TEST_BUILDDIR=' + meson.current_build_dir(),
]

test_programs = [
  'eos-payg-csv.py',
]

installed_tests_metadir = join_paths(datadir, 'installed-tests', 'eos-payg-csv')
installed_tests_execdir = join_paths(libexecdir, 'installed-tests',
                                     'eos-payg-csv')

foreach program: test_programs
  test_conf = configuration_data()
  test_conf.set('installed_tests_dir', installed_tests_execdir)
  test_conf.set('program', program)

  configure_file(
    input: test_template,
    output: program + '.test',
    install: enable_installed_tests,
    install_dir: installed_tests_metadir,
    configuration: test_conf,
  )

  main = files(program)
  if enable_installed_tests
    install_data(
      main,
      files('taptestrunner.py'),
      install_dir: installed_tests_execdir,
      install_mode: 'rwxr-xr-x',
    )
    install_data(
      main,
      files('short-key.csv', 'valid-2-keys-dupes.csv', 'wrong-columns.csv'),
      install_dir: installed_tests_metadir,
      install_mode: 'rw-r--r--',
    )
  endif

  test(
    program,
    py3,
    args: main,
    env: envs,
    suite: ['eos-payg'],
  )
endforeach
