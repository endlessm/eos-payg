eos_paygd_api_version = '1'
eos_paygd_sources = [
  'main.c',
]

eos_paygd_deps = [
  dependency('gio-2.0', version: '>= 2.44'),
  dependency('glib-2.0', version: '>= 2.44'),
  dependency('gobject-2.0', version: '>= 2.44'),
  dependency('libsystemd'),
  dependency('efivar'),
  libgsystemservice_dep,
  libeos_payg_dep,
]

executable('eos-paygd' + eos_paygd_api_version,
  eos_paygd_sources,
  dependencies: eos_paygd_deps,
  include_directories: root_inc,
  install: true,
  install_dir: get_option('libexecdir'),
  install_rpath: pkglibdir,
)

# systemd, polkit and D-Bus files
config = configuration_data()
config.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))

configure_file(
  input: 'com.endlessm.Payg1.conf.in',
  output: 'com.endlessm.Payg1.conf',
  install_dir: join_paths(get_option('datadir'), 'dbus-1', 'system.d'),
  configuration: config,
)
systemd = dependency('systemd')
systemdsystemunitdir = get_option('systemdsystemunitdir')
if systemdsystemunitdir == ''
  systemdsystemunitdir = systemd.get_pkgconfig_variable('systemdsystemunitdir')
endif

# eos-paygd service for Phase 2 PAYG systems
configure_file(
  input: 'eos-paygd.service.in',
  output: 'eos-paygd.service',
  install_dir: systemdsystemunitdir,
  configuration: config,
)
# eos-paygd service for Phase 4+ PAYG systems
configure_file(
  input: 'eos-paygd-2.service.in',
  output: 'eos-paygd-2.service',
  install_dir: systemdsystemunitdir,
  configuration: config,
)

# Documentation
install_man('docs/eos-paygd.8')
install_man('docs/eos-payg.conf.5')
