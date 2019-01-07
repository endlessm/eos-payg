get_clock_boottime_sources = [
  'get-clock-boottime.c',
]

executable('get-clock-boottime',
  get_clock_boottime_sources,
  include_directories: root_inc,
  install: true,
)

install_data('eos-payg-ctl',
  install_dir: bindir,
  install_mode: 'rwxr-xr-x',
)
