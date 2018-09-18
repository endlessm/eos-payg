suffixes = ['one', 'two']

deps = [
  dependency('gio-2.0', version: '>= 2.44'),
  dependency('glib-2.0', version: '>= 2.44'),
  dependency('gobject-2.0', version: '>= 2.44'),
  libeos_payg_dep,
]

test_provider_sources = [
  'test-provider.c',
  'test-provider.h',
]
libtest_provider = shared_library('eos-payg-test-provider',
  test_provider_sources,
  dependencies: deps,
)

libtest_provider_dep = declare_dependency(
  link_with: libtest_provider,
  include_directories: [
    root_inc,
  ],
)

foreach suffix: suffixes
  shared_module('epg-test-provider-' + suffix,
    'test-provider-' + suffix + '.c',
    dependencies: deps + [
      dependency('libpeas-1.0'),
      libtest_provider_dep,
    ],
  )

  plugin_conf = configuration_data()
  plugin_conf.set('suffix', suffix)
  configure_file(
    input: 'epg-test-provider.plugin.in',
    output: 'epg-test-provider-' + suffix + '.plugin',
    configuration: plugin_conf,
  )
endforeach
