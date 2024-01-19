eos_payg_generate_sources = [
  'main.c',
]

eos_payg_generate_deps = [
  glib_dep,
  gobject_dep,
  gio_dep,
  libeos_payg_codes_dep,
]

executable('eos-payg-generate-' + libeos_payg_codes_api_version,
  eos_payg_generate_sources,
  dependencies: eos_payg_generate_deps,
  install: true,
)

# Documentation
install_man('docs/eos-payg-generate.8')

subdir('tests')
