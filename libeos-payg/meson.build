libeos_payg_sources = [
  'boottime-source.c',
  'clock-jump-source.c',
  'clock.c',
  'efi.c',
  'errors.c',
  'fake-clock.c',
  'hwclock.c',
  'manager.c',
  'manager-service.c',
  'multi-task.c',
  'provider.c',
  'provider-loader.c',
  'real-clock.c',
  'service.c',
  'util.c',
]
libeos_payg_exported_headers = [
  'clock.h',
  'efi.h',
  'errors.h',
  'fake-clock.h',
  'multi-task.h',
  'provider.h',
  'real-clock.h',
  'util.h',
]
libeos_payg_headers = libeos_payg_exported_headers + [
  'boottime-source.h',
  'clock-jump-source.h',
  'manager.h',
  'manager-interface.h',
  'manager-service.h',
  'provider-loader.h',
  'service.h',
]

libeos_payg_deps = [
  glib_dep,
  gobject_dep,
  gio_dep,
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
