python_mod = import('python')
py3 = python_mod.find_installation('python3')

installed_tests_metadir = join_paths(datadir, 'installed-tests', 'eos-payg-admin')
installed_tests_execdir = join_paths(libexecdir, 'installed-tests', 'eos-payg-admin')

envs = test_env + [
  'PYTHONPATH=' + meson.current_source_dir() / '..',
]

test_programs = [
  'test-eos-gen-paygid.py',
]

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
      install_dir: installed_tests_execdir,
      install_mode: 'rwxr-xr-x',
    )
    install_data(
      files('../eos-gen-paygid'),
      install_dir: installed_tests_execdir,
    )
  endif

  test(
    program,
    py3,
    args: main,
    env: envs,
    suite: ['eos-payg-admin'],
  )
endforeach
