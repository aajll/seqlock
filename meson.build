project(
  'seqlock',
  'c',
  version: '0.1.1',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
  license: 'MIT',
)

project_description = 'A small Meson-based sequence lock library in C'

ver = meson.project_version().split('.')
major = ver[0].to_int()
minor = ver[1].to_int()
patch = ver[2].to_int()

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set('MAJOR', major)
conf.set('MINOR', minor)
conf.set('PATCH', patch)

generated_version_header = configure_file(
  input: 'config' / 'seqlock_version.h.in',
  output: 'seqlock_version.h',
  configuration: conf,
  install: true,
  install_dir: get_option('includedir') / 'seqlock',
)

public_headers = include_directories('include')
generated_headers = include_directories('.')

library_sources = files('src/seqlock.c')

seqlock_lib = both_libraries(
  'seqlock',
  library_sources,
  include_directories: [public_headers, generated_headers],
  version: meson.project_version(),
  soversion: major,
  install: true,
)

install_headers('include/seqlock.h', 'include/seqlock_conf.h', subdir: 'seqlock')

seqlock_dep = declare_dependency(
  include_directories: [public_headers, generated_headers],
  link_with: seqlock_lib.get_static_lib(),
  sources: generated_version_header,
)

meson.override_dependency('seqlock', seqlock_dep)

pkg = import('pkgconfig')
pkg.generate(
  seqlock_lib.get_static_lib(),
  description: project_description,
  filebase: 'seqlock',
  name: 'seqlock',
  subdirs: 'seqlock',
)

if get_option('build_tests')
  subdir('tests')
endif
