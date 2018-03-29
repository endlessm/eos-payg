eos_paygd_api_version = '1'
eos_paygd_sources = [
  'main.c',
]

eos_paygd_deps = [
  dependency('gio-2.0', version: '>= 2.44'),
  dependency('glib-2.0', version: '>= 2.44'),
  dependency('gobject-2.0', version: '>= 2.44'),
  libhelper_dep,
  libeos_payg_dep,
]

executable('eos-paygd' + eos_paygd_api_version,
  eos_paygd_sources,
  dependencies: eos_paygd_deps,
  include_directories: root_inc,
  install: true,
  install_dir: get_option('libexecdir'),
)

# systemd, polkit and D-Bus files
config = configuration_data()
config.set('DAEMON_USER', get_option('payg_daemon_user'))
config.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))
config.set('localstatedir', join_paths(get_option('prefix'), get_option('localstatedir')))

configure_file(
  input: 'com.endlessm.Payg1.conf.in',
  output: 'com.endlessm.Payg1.conf',
  install_dir: join_paths(get_option('datadir'), 'dbus-1', 'system.d'),
  configuration: config,
)
configure_file(
  input: 'com.endlessm.Payg1.service.in',
  output: 'com.endlessm.Payg1.service',
  install_dir: join_paths(get_option('datadir'), 'dbus-1', 'system-services'),
  configuration: config,
)
configure_file(
  input: 'eos-paygd.conf.in',
  output: 'eos-paygd.conf',
  install_dir: dependency('systemd').get_pkgconfig_variable('sysusersdir'),
  configuration: config,
)
configure_file(
  input: 'eos-paygd.service.in',
  output: 'eos-paygd.service',
  install_dir: dependency('systemd').get_pkgconfig_variable('systemdsystemunitdir'),
  configuration: config,
)

# Documentation
install_man('docs/eos-paygd.8')
install_man('docs/eos-payg.conf.5')

subdir('tests')
