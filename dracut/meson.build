dracut = dependency('dracut', required: true)
dracut_moddir = dracut.get_variable(pkgconfig: 'dracutmodulesdir') + '/50eos-payg'

config = configuration_data()
config.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))

configure_file(
  input: 'module-setup.sh.in',
  output: 'module-setup.sh',
  install_dir: dracut_moddir,
  configuration: config,
)
