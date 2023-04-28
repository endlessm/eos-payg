project('eos-payg','c',
  version: '0.2.4',
  meson_version: '>= 0.51.0',
  license: 'MPL-2.0 or LGPLv2.1+',
  default_options: [
    # We want all internal libraries, including libgsystemservice, to be
    # static.
    'default_library=static',
    'c_std=gnu11',
    'warning_level=3',
    'werror=true',
  ],
)

gnome = import('gnome')
pkgconfig = import('pkgconfig')

eos_payg_api_version = '1'
eos_payg_api_name = 'eos-payg-' + eos_payg_api_version

prefix = get_option('prefix')
bindir = join_paths(prefix, get_option('bindir'))
datadir = join_paths(prefix, get_option('datadir'))
libdir = join_paths(prefix, get_option('libdir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
localedir = join_paths(prefix, get_option('localedir'))
includedir = join_paths(prefix, get_option('includedir'))
localstatedir = join_paths(prefix, get_option('localstatedir'))
sysconfdir = join_paths(prefix, get_option('sysconfdir'))
pkglibdir = join_paths(libdir, eos_payg_api_name)
pluginsdir = join_paths(pkglibdir, 'plugins')

cc = meson.get_compiler('c')

config_h = configuration_data()
config_h.set_quoted('DATADIR', datadir)
config_h.set_quoted('GETTEXT_PACKAGE', meson.project_name())
config_h.set_quoted('LOCALEDIR', localedir)
config_h.set_quoted('LOCALSTATEDIR', localstatedir)
config_h.set_quoted('PREFIX', prefix)
config_h.set_quoted('SYSCONFDIR', sysconfdir)
config_h.set_quoted('PLUGINSDIR', pluginsdir)
config_h.set('SIZEOF_TIME_T', cc.sizeof('time_t', prefix : '#include <sys/time.h>'))
configure_file(
  output: 'config.h',
  configuration: config_h,
)
root_inc = include_directories('.')

# Translations support
i18n = import('i18n')

# Enable warning flags
test_args = [
  '-fno-strict-aliasing',
  '-fstack-protector-strong',
  '-Waggregate-return',
  '-Wall',
  '-Wunused',
  '-Warray-bounds',
  '-Wcast-align',
  '-Wclobbered',
  '-Wno-declaration-after-statement',
  '-Wduplicated-branches',
  '-Wduplicated-cond',
  '-Wempty-body',
  '-Wformat=2',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wformat-signedness',
  '-Wignored-qualifiers',
  '-Wimplicit-function-declaration',
  '-Wincompatible-pointer-types',
  '-Wincompatible-pointer-types-discards-qualifiers',
  '-Winit-self',
  '-Wint-conversion',
  '-Wlogical-op',
  '-Wmisleading-indentation',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wno-error=cpp',
  '-Wdiscarded-qualifiers',
  '-Wno-missing-field-initializers',
  '-Wno-suggest-attribute=format',
  '-Wno-unused-parameter',
  '-Wnull-dereference',
  '-Wold-style-definition',
  '-Woverflow',
  '-Woverride-init',
  '-Wparentheses',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wreturn-type',
  '-Wshadow',
  '-Wsign-compare',
  '-Wstrict-aliasing=2',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wswitch-enum',
  '-Wtype-limits',
  '-Wundef',
  '-Wuninitialized',
  '-Wunused-but-set-variable',
  '-Wunused-result',
  '-Wunused-variable',
  '-Wwrite-strings'
]
cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments(test_args), language: 'c')

# Apply -Wno-pedantic to subprojects too
add_global_arguments(cc.get_supported_arguments(['-Wno-pedantic']), language: 'c')

want_tests = get_option('tests')
enable_installed_tests = get_option('installed_tests')
test_template = files('template.test.in')
test_env = [
  'G_DEBUG=gc-friendly',
  'MALLOC_CHECK_=2',
  'LC_ALL=C.UTF-8',
]

libgsystemservice = subproject('libgsystemservice')
libgsystemservice_dep = libgsystemservice.get_variable('libgsystemservice_dep')

subdir('libeos-payg-codes')
subdir('libeos-payg')
subdir('eos-paygd')
subdir('eos-payg-csv')
subdir('eos-payg-ctl')
subdir('eos-payg-generate')
subdir('po')
subdir('provision-phase-1')
subdir('dracut')
