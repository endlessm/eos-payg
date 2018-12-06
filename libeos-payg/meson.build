libeos_payg_sources = [
  'errors.c',
  'manager.c',
  'manager-service.c',
  'multi-task.c',
  'provider.c',
  'provider-loader.c',
  'service.c',
]
libeos_payg_exported_headers = [
  'errors.h',
  'multi-task.h',
  'provider.h',
]
libeos_payg_headers = libeos_payg_exported_headers + [
  'manager.h',
  'manager-service.h',
  'provider-loader.h',
  'service.h',
]

# FIXME: once we can depend on gio >= 2.58.3 (earlier point releases aren't
# enough, due to https://gitlab.gnome.org/GNOME/glib/merge_requests/514),
# make this required, delete the fallback block, and delete the checked-in
# copies of manager-interface.[ch].
gio_dep = dependency('gio-2.0', version: '>= 2.58.3', required: false)
if gio_dep.found()
  warning('Looks like Endless now has GLib >= 2.58.3; please update libeos-payg/meson.build accordingly')

  # TODO: add --interface-info-{header,body} support to Meson's 'gnome' module?
  gdbus_codegen = gio_dep.get_pkgconfig_variable('gdbus_codegen')

  manager_interface_c = custom_target('manager-interface-c',
    input: ['com.endlessm.Payg1.xml'],
    output: ['manager-interface.c'],
    command: [gdbus_codegen, '--interface-info-body',
              '--output', '@OUTPUT@',
              '--interface-prefix', 'com.endlessm',
              '--c-namespace', 'epg',
              '--pragma-once',
              '@INPUT@'])
  libeos_payg_sources += [manager_interface_c]

  manager_interface_h = custom_target('manager-interface-h',
    input: ['com.endlessm.Payg1.xml'],
    output: ['manager-interface.h'],
    command: [gdbus_codegen, '--interface-info-header',
              '--output', '@OUTPUT@',
              '--interface-prefix', 'com.endlessm',
              '--c-namespace', 'epg',
              '--pragma-once',
              '@INPUT@'])
  libeos_payg_headers += [manager_interface_h]

  diff = find_program('diff')
  test('generated header matches',
    diff, args: files('manager-interface.h') + [manager_interface_h])
  test('generated body matches',
    diff, args: files('manager-interface.c') + [manager_interface_c])
else
  gio_dep = dependency('gio-2.0', version: '>= 2.54')
  libeos_payg_sources += ['manager-interface.c']
  libeos_payg_headers += ['manager-interface.h']
endif

libeos_payg_deps = [
  gio_dep,
  dependency('glib-2.0', version: '>= 2.54'),
  dependency('gobject-2.0', version: '>= 2.54'),
  dependency('libpeas-1.0'),
  libeos_payg_codes_dep,
  libgsystemservice_dep,
]

libeos_payg_resources = gnome.compile_resources(
  'resources',
  'libeos-payg.gresource.xml',
  c_name : 'epg',
)

libeos_payg = shared_library(eos_payg_api_name,
  libeos_payg_sources + libeos_payg_headers + libeos_payg_resources,
  dependencies: libeos_payg_deps,
  include_directories: root_inc,
  install: true,
  install_dir: pkglibdir,
)
libeos_payg_dep = declare_dependency(
  link_with: libeos_payg,
  include_directories: root_inc,
)

libeos_payg_include_subdir = join_paths(eos_payg_api_name, 'libeos-payg')
install_headers(libeos_payg_exported_headers,
  subdir: libeos_payg_include_subdir,
)

# Can't use the shorthand where we pass the shared_library() as the first
# argument to inherit its name because this also inherits its install_dir; but
# we want the .pc file to end up in the standard pkgconfig path, not a
# pkgconfig file inside pkglibdir (which will not be searched by pkg-config).
pkgconfig.generate(
  name: eos_payg_api_name,
  description: 'Pay-as-you-go support for Endless OS',
  version: meson.project_version(),
  libraries: libeos_payg,
  subdirs: eos_payg_api_name,
  variables: [
    'pluginsdir=' + pluginsdir,
    # Plugins need this to be able to set their rpath correctly.
    # https://github.com/mesonbuild/meson/issues/4372
    'pkglibdir=' + pkglibdir,
  ],
  # According to http://mesonbuild.com/Pkgconfig-module.html#pkggenerate,
  # libeos_payg's dependencies will automatically be added to Requires.private
  # with Meson >= 0.45, but this doesn't seem true in my testing.
  # https://github.com/mesonbuild/meson/issues/2896#issuecomment-428529445
  requires: [ 'gio-2.0', 'glib-2.0', 'gobject-2.0' ],
)

subdir('tests')
