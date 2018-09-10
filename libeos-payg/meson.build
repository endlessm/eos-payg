libeos_payg_api_version = '1'
libeos_payg_sources = [
  'boottime-source.c',
  'manager.c',
  'manager-service.c',
  'service.c',
]
libeos_payg_headers = [
  'boottime-source.h',
  'manager.h',
  'manager-interface.h',
  'manager-service.h',
  'service.h',
]

libeos_payg_deps = [
  dependency('gio-2.0', version: '>= 2.46'),
  dependency('glib-2.0', version: '>= 2.54'),
  dependency('gobject-2.0', version: '>= 2.54'),
  libeos_payg_codes_dep,
  libgsystemservice_dep,
]

libeos_payg_resources = gnome.compile_resources(
  'resources',
  'libeos-payg.gresource.xml',
  c_name : 'epg',
)

libeos_payg = static_library('eos-payg-' + libeos_payg_api_version,
  libeos_payg_sources + libeos_payg_headers + libeos_payg_resources,
  dependencies: libeos_payg_deps,
  include_directories: root_inc,
  install: false,
)
libeos_payg_dep = declare_dependency(
  link_with: libeos_payg,
  include_directories: root_inc,
)

subdir('tests')
