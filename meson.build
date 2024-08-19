project(
  'dragman',
  'c',
  version: '0.0.1',
  license: 'GPL-2.0-only',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
  ],
)

add_project_arguments(meson.get_compiler('c').get_supported_arguments([
  '-Wundef',
  '-Wlogical-op',
  '-Wmissing-include-dirs',
  '-Wold-style-definition',
  '-Wpointer-arith',
  '-Winit-self',
  '-Wstrict-prototypes',
  '-Wimplicit-fallthrough=2',
  '-Wendif-labels',
  '-Wstrict-aliasing=2',
  '-Woverflow',
  '-Wmissing-prototypes',
  '-Walloca',
  '-Wunused-macros',

  '-Wno-unused-parameter',
]), language: 'c')

executable(
  meson.project_name(),
  'main.c',
  include_directories: [],
  dependencies: [
    dependency('libevdev'),
    dependency('libinput'),
    dependency('libuv'),
    dependency('libudev'),
  ],
)
