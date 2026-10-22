# SPDX-License-Identifier: MIT

project(
  'seqlock',
  'c',
  version: '0.2.0',
  meson_version: '>=0.57.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
  license: 'MIT',
)

project_description = 'A small Meson-based sequence lock library in C'

# include/seqlock.h defines the version macros, so a copy-in build has them.
# Configuration fails when they differ from the project version.

fs = import('fs')
version_parts = meson.project_version().split('.')
version_macros = {
  'SEQLOCK_VERSION_MAJOR': version_parts[0],
  'SEQLOCK_VERSION_MINOR': version_parts[1],
  'SEQLOCK_VERSION_PATCH': version_parts[2],
  'SEQLOCK_VERSION': '"' + meson.project_version() + '"',
}
header_lines = []
foreach line : fs.read('include' / 'seqlock.h').split('\n')
  header_lines += ' '.join(line.split())
endforeach
foreach name, value : version_macros
  if '#define @0@ @1@'.format(name, value) not in header_lines
    error('include/seqlock.h must define @0@ as @1@'.format(name, value))
  endif
endforeach

public_headers = include_directories('include')

library_sources = files('src/seqlock.c')

seqlock_lib = static_library(
  'seqlock',
  library_sources,
  include_directories: public_headers,
  install: true,
)

install_headers(
  'include/seqlock.h',
  'include/seqlock_conf.h',
  subdir: 'seqlock',
)

seqlock_dep = declare_dependency(
  include_directories: public_headers,
  link_with: seqlock_lib,
)

meson.override_dependency('seqlock', seqlock_dep)

pkg = import('pkgconfig')
pkg.generate(
  seqlock_lib,
  description: project_description,
  filebase: 'seqlock',
  name: 'seqlock',
  subdirs: 'seqlock',
)

if get_option('build_tests')
  subdir('tests')
endif
