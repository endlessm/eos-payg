libeos_payg_codes_api_version = '1'
libeos_payg_codes_sources = [
  'codes.c',
]
libeos_payg_codes_headers = [
  'codes.h',
]

libeos_payg_codes_deps = [
  glib_dep,
  gobject_dep,
  gio_dep,
]

libeos_payg_codes = static_library('eos-payg-codes-' + libeos_payg_codes_api_version,
  libeos_payg_codes_sources + libeos_payg_codes_headers,
  dependencies: libeos_payg_codes_deps,
  include_directories: root_inc,
  install: false,
)
libeos_payg_codes_dep = declare_dependency(
  link_with: libeos_payg_codes,
  include_directories: root_inc,
)

subdir('tests')
