libeos_payg_api_version = '1'
libeos_payg_sources = [
  'manager.c',
  'manager-service.c',
  'service.c',
]
libeos_payg_headers = [
  'manager.h',
  'manager-interface.h',
  'manager-service.h',
  'service.h',
]

libeos_payg_deps = [
  dependency('gio-2.0', version: '>= 2.46'),
  dependency('glib-2.0', version: '>= 2.54'),
  dependency('gobject-2.0', version: '>= 2.54'),
  libhelper_dep,
]

libeos_payg = static_library('eos-payg-' + libeos_payg_api_version,
  libeos_payg_sources + libeos_payg_headers,
  dependencies: libeos_payg_deps,
  include_directories: root_inc,
  install: false,
)
libeos_payg_dep = declare_dependency(
  link_with: libeos_payg,
  include_directories: root_inc,
)

subdir('tests')
