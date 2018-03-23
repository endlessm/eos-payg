eos_payg_generate_sources = [
  'main.c',
]

eos_payg_generate_deps = [
  dependency('gio-2.0', version: '>= 2.44'),
  dependency('glib-2.0', version: '>= 2.50'),
  dependency('gobject-2.0', version: '>= 2.44'),
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
